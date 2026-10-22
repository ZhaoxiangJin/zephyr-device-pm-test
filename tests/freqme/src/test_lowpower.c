/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The sysmanaged and dpd layers: one forced transition with a monitor running.
 *
 * The sequence is the same in both and only the state entered differs, which
 * pm_test_enter_transition() picks from the layer symbol.
 *
 * Before the transition the reference is switched to FRO_12M, so the route the
 * driver has to put back is the one the consumer selected and not the boot
 * default, and WINDOW monitoring is started. Then MIN is overwritten behind the
 * driver's back with a value the driver does not hold in RAM. That one register
 * tells the two layers apart:
 *
 * - across Deep Sleep the block keeps its power and nothing rewrites MIN, so the
 *   overwritten value has to still be there;
 * - across Deep Power Down the block loses its registers and TURN_ON rebuilds them
 *   from what configure() applied, so the overwritten value has to be gone and the
 *   configured one back. That is what shows both that power was really cut and that
 *   the replay ran.
 *
 * In both layers the rest of the configuration and the route must come back, and
 * the monitor the consumer started must be running again without another start().
 */

#include <zephyr/drivers/clock_monitor.h>
#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>

#include <pm_test/lowpower.h>
#include <pm_test/state.h>

#include "freqme.h"

ZTEST(freqme_pm, test_monitor_across_transition)
{
	const struct device *dev = freqme_dev();
	struct freqme_image running;
	struct freqme_image after;
	uint32_t sentinel;
	uint32_t hz = 0U;
	bool measuring_after;
	bool continuous_after;

	/* No runtime PM in these layers, so the sweep is the only thing that will
	 * suspend the device.
	 */
	zassert_pm_state(dev, PM_DEVICE_STATE_ACTIVE);

	zassert_ok(clock_monitor_set_source(dev, freqme_alt_ref_cookie(), 0U),
		   "%s: could not select the FRO_12M reference", dev->name);
	zassert_ok(freqme_start_window(false), "%s: could not start WINDOW", dev->name);
	freqme_image_read(&running);
	zassert_equal(running.route_ref, freqme_alt_ref_route(),
		      "%s: FREQMEAS_REF holds %u after set_source(), want %u", dev->name,
		      running.route_ref, freqme_alt_ref_route());

	sentinel = running.min ^ 1U;
	freqme_min_write(sentinel);
	zassert_equal(freqme_min(), sentinel, "%s: the MIN overwrite did not take", dev->name);
	freqme_report_hw("before");

	pm_test_enter_transition();

	/* Sampled before the console is revived: reviving it takes time, and the
	 * question is what the block looked like the moment the sweep handed it back.
	 */
	measuring_after = freqme_measuring();
	continuous_after = freqme_continuous();
	freqme_image_read(&after);

	pm_test_console_resume(PM_TEST_CONSOLE_REINIT_PINMUX);
	TC_PRINT("resumed from %s\n", PM_TEST_TRANSITION_NAME);
	freqme_report_hw("after-resume");

	zassert_pm_state(dev, PM_DEVICE_STATE_ACTIVE);

#if defined(CONFIG_PM_TEST_LAYER_DPD)
	zassert_not_equal(after.min, sentinel,
			  "%s: the overwritten MIN survived %s, so the block kept its power",
			  dev->name, PM_TEST_TRANSITION_NAME);
	zassert_equal(after.min, running.min,
		      "%s: MIN is 0x%08x after %s, want the configured 0x%08x", dev->name,
		      after.min, PM_TEST_TRANSITION_NAME, running.min);
#else
	zassert_equal(after.min, sentinel,
		      "%s: MIN is 0x%08x after %s, want the untouched 0x%08x", dev->name,
		      after.min, PM_TEST_TRANSITION_NAME, sentinel);
#endif

	zassert_equal(after.ctrl, running.ctrl,
		      "%s: CTRLSTAT config 0x%08x after %s, want 0x%08x", dev->name, after.ctrl,
		      PM_TEST_TRANSITION_NAME, running.ctrl);
	zassert_equal(after.max, running.max, "%s: MAX 0x%08x after %s, want 0x%08x", dev->name,
		      after.max, PM_TEST_TRANSITION_NAME, running.max);
	zassert_equal(after.route_ref, running.route_ref,
		      "%s: FREQMEAS_REF %u after %s, want the selected %u", dev->name,
		      after.route_ref, PM_TEST_TRANSITION_NAME, running.route_ref);
	zassert_equal(after.route_tar, running.route_tar,
		      "%s: FREQMEAS_TAR %u after %s, want %u", dev->name, after.route_tar,
		      PM_TEST_TRANSITION_NAME, running.route_tar);
	zassert_true(measuring_after && continuous_after,
		     "%s: monitoring was not re-armed after %s", dev->name,
		     PM_TEST_TRANSITION_NAME);

	/* And the block has to work, not just read back right. */
	(void)clock_monitor_stop(dev);
	zassert_ok(freqme_measure_once(FREQME_WINDOW_NS, &hz),
		   "%s: a measurement after %s failed", dev->name, PM_TEST_TRANSITION_NAME);
	zassert_true(freqme_rate_plausible(hz), "%s: measured %u Hz after %s", dev->name, hz,
		     PM_TEST_TRANSITION_NAME);
}
