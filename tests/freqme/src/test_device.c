/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The device layer: the driver's PM callbacks, driven directly.
 *
 * Three tests. The first is the SUSPEND/RESUME round trip on a running WINDOW
 * monitor, configured against a rate twice the target's so that every window
 * reports: whether callbacks arrive is then a direct reading of whether the
 * hardware is monitoring. A suspend has to stop them and a resume has to bring
 * them back without the consumer calling clock_monitor_start() again.
 *
 * The second is a MEASURE started after a suspend and a resume with no power loss
 * in between. The suspend clears the interrupt enables, so this is the case that
 * shows whether the resume put them back: without them the measurement still runs
 * and its callback never comes.
 *
 * The third is TURN_OFF/TURN_ON, which owns the register block. Nothing in this
 * layer really cuts power, so the block and its INPUTMUX route are clobbered by
 * hand for the restore to have something to put back. What a real power cycle does
 * is asked in src/test_lowpower.c under the dpd layer.
 */

#include <zephyr/drivers/clock_monitor.h>
#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "freqme.h"

ZTEST(freqme_pm, test_device_suspend_resume_window)
{
	const struct device *dev = freqme_dev();
	struct freqme_image running;
	struct freqme_image after;
	int err;

	/* No runtime PM in this layer, so pm_device_driver_init() resumed it. */
	zassert_pm_state(dev, PM_DEVICE_STATE_ACTIVE);

	zassert_ok(freqme_start_window(true), "%s: could not start WINDOW", dev->name);
	zassert_true(freqme_window_events_arrive(50U),
		     "%s: a faulty WINDOW reported nothing before the suspend", dev->name);
	freqme_image_read(&running);
	freqme_report_hw("before-suspend");

	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND),
		   "%s: SUSPEND action rejected", dev->name);
	zassert_pm_state(dev, PM_DEVICE_STATE_SUSPENDED);
	freqme_report_hw("after-suspend");

	/* The claim of this layer: the block really stopped, not just the driver's
	 * state variable.
	 */
	zassert_false(freqme_measuring(), "%s: still measuring after SUSPEND", dev->name);
	zassert_false(freqme_continuous(), "%s: continuous mode still on after SUSPEND",
		      dev->name);
	zassert_equal(freqme_int_enables(), 0U,
		      "%s: interrupt enables 0x%x left set after SUSPEND", dev->name,
		      freqme_int_enables());
	zassert_false(freqme_window_events_arrive(20U),
		      "%s: WINDOW callbacks kept arriving while suspended", dev->name);

	/* A suspended device refuses what would write its register block. */
	err = clock_monitor_start(dev);
	zassert_equal(err, -EBUSY, "%s: start() on a suspended device returned %d", dev->name,
		      err);
	err = freqme_start_window(true);
	zassert_equal(err, -EBUSY, "%s: configure() on a suspended device returned %d",
		      dev->name, err);
	err = clock_monitor_set_source(dev, freqme_alt_ref_cookie(), 0U);
	zassert_equal(err, -EBUSY, "%s: set_source() on a suspended device returned %d",
		      dev->name, err);

	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME),
		   "%s: RESUME action rejected", dev->name);
	zassert_pm_state(dev, PM_DEVICE_STATE_ACTIVE);
	freqme_image_read(&after);
	freqme_report_hw("after-resume");

	/*
	 * The monitor the consumer started is running again, with the
	 * configuration it had, and reports -- all without another start().
	 */
	zassert_true(freqme_measuring() && freqme_continuous(),
		     "%s: RESUME did not re-arm continuous monitoring", dev->name);
	zassert_true(freqme_image_equal(&after, &running),
		     "%s: RESUME left CTRLSTAT config 0x%08x MIN 0x%08x MAX 0x%08x, want "
		     "0x%08x/0x%08x/0x%08x", dev->name, after.ctrl, after.min, after.max,
		     running.ctrl, running.min, running.max);
	zassert_true(freqme_window_events_arrive(50U),
		     "%s: WINDOW reported nothing after RESUME", dev->name);

	/* A redundant action must be refused, not obeyed and not fatal. */
	err = pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME);
	zassert_equal(err, -EALREADY, "%s: second RESUME returned %d, want -EALREADY",
		      dev->name, err);

	(void)clock_monitor_stop(dev);
}

ZTEST(freqme_pm, test_device_measure_after_resume)
{
	const struct device *dev = freqme_dev();
	uint32_t before = 0U;
	uint32_t retained = 0U;
	uint32_t after = 0U;
	int suspend_rc;
	int retained_rc;
	int stop_rc;
	int resume_rc;
	int measure_rc;

	zassert_pm_state(dev, PM_DEVICE_STATE_ACTIVE);
	zassert_ok(freqme_measure_once(FREQME_WINDOW_NS, &before),
		   "%s: the first measurement failed", dev->name);

	suspend_rc = pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND);
	retained_rc = clock_monitor_get_rate(dev, &retained);
	stop_rc = clock_monitor_stop(dev);
	resume_rc = pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME);
	freqme_report_hw("after-resume");

	/* Started from the same configuration: no configure() in between. */
	measure_rc = freqme_start_and_wait(100U);
	if (measure_rc == 0) {
		(void)clock_monitor_get_rate(dev, &after);
	}

	zassert_ok(suspend_rc, "%s: SUSPEND rejected (%d)", dev->name, suspend_rc);
	/* The result is answered from RAM, so a suspend does not take it away. */
	zassert_ok(retained_rc, "%s: get_rate() on a suspended device returned %d", dev->name,
		   retained_rc);
	zassert_equal(retained, before, "%s: get_rate() returned %u while suspended, %u before",
		      dev->name, retained, before);
	zassert_ok(stop_rc, "%s: stop() on a suspended device returned %d", dev->name, stop_rc);
	zassert_ok(resume_rc, "%s: RESUME rejected (%d)", dev->name, resume_rc);

	/*
	 * The claim. The measurement runs either way; its callback only arrives if
	 * RESUME restored the result-ready interrupt enable the suspend cleared.
	 */
	zassert_ok(measure_rc,
		   "%s: a measurement started after the resume did not complete (%d)",
		   dev->name, measure_rc);
	zassert_true(freqme_rate_plausible(after), "%s: measured %u Hz after the resume",
		     dev->name, after);
}

ZTEST(freqme_pm, test_device_turn_on_rebuilds_the_register_block)
{
	const struct device *dev = freqme_dev();
	struct freqme_image running;
	struct freqme_image clobbered;
	struct freqme_image after_turn_on;
	bool measuring_after_turn_on;
	int suspend_rc;
	int turn_off_rc;
	int turn_on_rc;
	int resume_rc;

	zassert_pm_state(dev, PM_DEVICE_STATE_ACTIVE);
	zassert_ok(freqme_start_window(true), "%s: could not start WINDOW", dev->name);
	freqme_image_read(&running);

	/*
	 * Collected in one go and asserted afterwards, so that a failure cannot
	 * hand the next test a device the core still records as OFF.
	 */
	suspend_rc = pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND);
	freqme_clobber(&clobbered);
	turn_off_rc = pm_device_action_run(dev, PM_DEVICE_ACTION_TURN_OFF);
	turn_on_rc = pm_device_action_run(dev, PM_DEVICE_ACTION_TURN_ON);
	measuring_after_turn_on = freqme_measuring();
	freqme_image_read(&after_turn_on);
	freqme_report_hw("after-turn-on");
	resume_rc = pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME);
	freqme_report_hw("after-resume");

	zassert_ok(suspend_rc, "%s: SUSPEND rejected (%d)", dev->name, suspend_rc);
	zassert_ok(turn_off_rc, "%s: TURN_OFF rejected (%d)", dev->name, turn_off_rc);
	zassert_ok(turn_on_rc, "%s: TURN_ON rejected (%d)", dev->name, turn_on_rc);
	zassert_ok(resume_rc, "%s: RESUME rejected (%d)", dev->name, resume_rc);
	zassert_pm_state(dev, PM_DEVICE_STATE_ACTIVE);

	/* Before anything else: a clobber that did not take would make the claims
	 * below answerable without the block ever having been disturbed.
	 */
	zassert_false(freqme_image_equal(&clobbered, &running), "%s: the clobber did not take",
		      dev->name);

	/*
	 * TURN_ON returns with the core about to record the device as suspended,
	 * so a block that started measuring would be running behind a PM state that
	 * says it is not.
	 */
	zassert_false(measuring_after_turn_on, "%s: TURN_ON left the block measuring",
		      dev->name);

	/* The rebuild, registers and route both. */
	zassert_true(freqme_image_equal(&after_turn_on, &running),
		     "%s: TURN_ON rebuilt CTRLSTAT config 0x%08x MIN 0x%08x MAX 0x%08x route "
		     "%u/%u, want 0x%08x/0x%08x/0x%08x route %u/%u",
		     dev->name, after_turn_on.ctrl, after_turn_on.min, after_turn_on.max,
		     after_turn_on.route_ref, after_turn_on.route_tar, running.ctrl,
		     running.min, running.max, running.route_ref, running.route_tar);

	/* And the monitor the consumer had started is monitoring again. */
	zassert_true(freqme_measuring() && freqme_continuous(),
		     "%s: RESUME did not re-arm monitoring after the rebuild", dev->name);
	zassert_true(freqme_window_events_arrive(50U),
		     "%s: WINDOW reported nothing after the rebuild", dev->name);

	(void)clock_monitor_stop(dev);
}
