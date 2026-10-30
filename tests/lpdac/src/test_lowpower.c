/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The sysmanaged and dpd layers: the device sweep across one transition.
 *
 * One file for two layers, because the sequence is identical and only the state
 * being entered differs -- and that difference is what separates the two claims.
 * In sysmanaged the sweep's SUSPEND and RESUME are all that happen, so putting the
 * output back is enough. In dpd the CORE domain is power gated: GCR comes back at
 * its reset value and nothing re-runs driver init, so the domain's TURN_ON has to
 * rebuild the devicetree configuration before RESUME can put the output back on top
 * of it. dpd is the only layer that can catch a driver with no restore path.
 *
 * The application's part is the reference held across the transition, which is why
 * the sequence below takes one: this driver never resumes itself, so an output that
 * nobody is holding is not one the sweep owes anything to.
 */

#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>

#include <pm_test/lowpower.h>
#include <pm_test/state.h>

#include "lpdac.h"

ZTEST(lpdac_pm, test_sweep_across_transition)
{
	uint32_t config;
	uint32_t config_after;
	uint32_t gcr_after;
	bool on_after;
	bool off_for_report = false;
	int report_quiesce_ret = 0;
	const bool shared_console_pad =
		DT_PROP_OR(LPDAC_USER_NODE, dac_console_shared_pad, false);

	/* No runtime PM in these layers, so the sweep is the only thing that will
	 * suspend the device.
	 */
	zassert_pm_state(LPDAC_DEV, PM_DEVICE_STATE_ACTIVE);

	zassert_ok(lpdac_setup(), "channel setup failed before the transition");
	zassert_ok(lpdac_write(LPDAC_MIDSCALE), "write failed before the transition");
	zassert_true(lpdac_output_on(), "output buffer off before the transition");
	config = lpdac_gcr_config();
	lpdac_report_hw("before");

	pm_test_enter_transition();

	/* Sampled before the console is revived: reviving it takes time, and the
	 * question is what the block looked like the moment the sweep handed it back.
	 */
	config_after = lpdac_gcr_config();
	on_after = lpdac_output_on();
	gcr_after = lpdac_gcr();

	if (shared_console_pad) {
		/* Snapshot first, then release the shared pad. */
		report_quiesce_ret = lpdac_setup();
		if (report_quiesce_ret == 0) {
			off_for_report = lpdac_output_off();
		}
	}

	pm_test_console_resume(PM_TEST_CONSOLE_REINIT_PINMUX);
	TC_PRINT("resumed from %s\n", PM_TEST_TRANSITION_NAME);
	lpdac_report_hw_snapshot("after-resume", gcr_after);
	if (shared_console_pad) {
		lpdac_report_hw("report-quiesced");
		zassert_ok(report_quiesce_ret,
			   "cannot release the DAC/UART shared pad");
		zassert_true(off_for_report,
			     "DAC output still drives the console pad");
	}

	/*
	 * pm_resume_devices() walks the devices it suspended in init order, so the
	 * core domain's TURN_ON has already run by the time the DAC's own RESUME does.
	 * The two together have to leave the output the consumer was holding.
	 */
	zassert_pm_state(LPDAC_DEV, PM_DEVICE_STATE_ACTIVE);
	zassert_equal(config_after, config,
		      "the configuration did not survive the transition: 0x%08x, want 0x%08x",
		      config_after, config);
	zassert_true(on_after, "the output was not put back for a consumer that never let go");
}
