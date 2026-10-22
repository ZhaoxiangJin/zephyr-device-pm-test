/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The runtime layer: the consumer owns the reference.
 *
 * This driver takes no runtime reference of its own, in either mode, so the
 * consumer holds one for as long as it wants the monitor. Three claims: a call that
 * would write the register block is refused rather than silently lost while no
 * reference is held, the route is applied once the first reference is, and what the
 * consumer configured comes back with the reference -- a measured result that is
 * still readable while suspended, a MEASURE that still completes, and a WINDOW
 * monitor that resumes on its own.
 */

#include <zephyr/drivers/clock_monitor.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "freqme.h"

ZTEST(freqme_pm, test_runtime_boots_without_a_consumer)
{
	const struct device *dev = freqme_dev();
	enum pm_device_state state;
	uint32_t want_ref;
	uint32_t want_tar;
	uint32_t got_ref;
	uint32_t got_tar;
	int err;

	zassert_true(pm_device_runtime_is_enabled(dev), "%s: runtime PM is not enabled",
		     dev->name);

	/*
	 * Two boot states are legal. Without a power domain the device lands in
	 * SUSPENDED; with one it lands in OFF, because the domain is
	 * runtime-suspended before FREQME initialises and TURN_ON was skipped --
	 * pd-soc-state-change-no-turn-on-under-runtime-pm in docs/findings.md.
	 */
	zassert_ok(pm_device_state_get(dev, &state), "%s: pm_device_state_get() failed",
		   dev->name);
	zassert_true((state == PM_DEVICE_STATE_SUSPENDED) || (state == PM_DEVICE_STATE_OFF),
		     "%s booted %s, want SUSPENDED or OFF under runtime PM", dev->name,
		     pm_device_state_str(state));

	/* An unwrapped call is refused, not silently lost. */
	err = freqme_configure_measure(FREQME_WINDOW_NS);
	zassert_equal(err, -EBUSY, "%s: configure() without a reference returned %d",
		      dev->name, err);

	/*
	 * Booted OFF, the device has had no TURN_ON yet. Taking the first reference
	 * resumes the power domain, which turns on every device under it before
	 * this one is resumed -- so by the time the reference is held the selected
	 * sources are routed, whichever state it booted in.
	 */
	zassert_ok(pm_device_runtime_get(dev), "%s: runtime_get rejected", dev->name);
	freqme_default_route(&want_ref, &want_tar);
	freqme_route_read(&got_ref, &got_tar);
	(void)pm_device_runtime_put(dev);

	zassert_equal(got_ref, want_ref,
		      "%s: FREQMEAS_REF %u once the first reference is held, want %u",
		      dev->name, got_ref, want_ref);
	zassert_equal(got_tar, want_tar,
		      "%s: FREQMEAS_TAR %u once the first reference is held, want %u",
		      dev->name, got_tar, want_tar);
}

ZTEST(freqme_pm, test_runtime_measure_survives_a_put)
{
	const struct device *dev = freqme_dev();
	uint32_t before = 0U;
	uint32_t retained = 0U;
	uint32_t after = 0U;

	zassert_ok(pm_device_runtime_get(dev), "%s: runtime_get rejected", dev->name);
	zassert_pm_state(dev, PM_DEVICE_STATE_ACTIVE);
	zassert_ok(freqme_measure_once(FREQME_WINDOW_NS, &before),
		   "%s: the measurement failed under a held reference", dev->name);

	/* A second reference must not be a second resume, and releasing it must not
	 * suspend a device somebody else still holds.
	 */
	zassert_ok(pm_device_runtime_get(dev), "%s: nested runtime_get rejected", dev->name);
	zassert_ok(pm_device_runtime_put(dev), "%s: first runtime_put rejected", dev->name);
	zassert_pm_state(dev, PM_DEVICE_STATE_ACTIVE);

	zassert_ok(pm_device_runtime_put(dev), "%s: last runtime_put rejected", dev->name);
	zassert_pm_state(dev, PM_DEVICE_STATE_SUSPENDED);
	freqme_report_hw("after-put");

	/* The result the consumer was handed stays readable while suspended. */
	zassert_ok(clock_monitor_get_rate(dev, &retained),
		   "%s: get_rate() refused on a suspended device", dev->name);
	zassert_equal(retained, before, "%s: get_rate() returned %u while suspended, %u before",
		      dev->name, retained, before);

	/*
	 * The consumer gets the configured measurement back by taking the reference
	 * again, without configuring again. Its callback only arrives if the resume
	 * put back the interrupt enable the suspend cleared.
	 */
	zassert_ok(pm_device_runtime_get(dev), "%s: runtime_get after the put rejected",
		   dev->name);
	zassert_ok(freqme_start_and_wait(100U),
		   "%s: a measurement started after the reference came back did not complete",
		   dev->name);
	zassert_ok(clock_monitor_get_rate(dev, &after), "%s: no result after re-get",
		   dev->name);
	zassert_true(freqme_rate_plausible(after), "%s: measured %u Hz after re-get", dev->name,
		     after);
	zassert_ok(pm_device_runtime_put(dev), "%s: final runtime_put rejected", dev->name);
}

ZTEST(freqme_pm, test_runtime_window_follows_the_reference)
{
	const struct device *dev = freqme_dev();
	bool reported_while_held;
	bool reported_while_released;
	bool reported_after_reget;

	zassert_ok(pm_device_runtime_get(dev), "%s: runtime_get rejected", dev->name);
	zassert_ok(freqme_start_window(true), "%s: could not start WINDOW", dev->name);
	reported_while_held = freqme_window_events_arrive(50U);

	/* The last reference goes away, so the driver suspends and monitoring stops. */
	zassert_ok(pm_device_runtime_put(dev), "%s: runtime_put rejected", dev->name);
	reported_while_released = freqme_window_events_arrive(20U);
	freqme_report_hw("after-put");

	/* Taking it back re-arms the monitor the consumer started, without a start(). */
	zassert_ok(pm_device_runtime_get(dev), "%s: runtime_get after the put rejected",
		   dev->name);
	reported_after_reget = freqme_window_events_arrive(50U);
	freqme_report_hw("after-reget");

	(void)clock_monitor_stop(dev);
	(void)pm_device_runtime_put(dev);

	zassert_true(reported_while_held, "%s: WINDOW reported nothing under a reference",
		     dev->name);
	zassert_false(reported_while_released,
		      "%s: WINDOW kept reporting after the last reference was dropped",
		      dev->name);
	zassert_true(reported_after_reget,
		     "%s: WINDOW did not resume when the reference was taken back", dev->name);
}
