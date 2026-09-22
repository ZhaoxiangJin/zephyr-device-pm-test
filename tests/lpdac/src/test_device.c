/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The device layer: the driver's PM callbacks, driven directly.
 *
 * Two tests, and they are two because they are about two different halves of the
 * callback. The first is the SUSPEND/RESUME round trip, which owns the output
 * buffer. The second is TURN_OFF/TURN_ON, which owns the register block -- and
 * since nothing in this layer really cut power to it, the block is cleared by hand
 * so that TURN_ON has something to restore. What a real power cycle does is asked
 * in src/test_lowpower.c under the dpd layer.
 */

#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "lpdac.h"

ZTEST(lpdac_pm, test_device_suspend_resume)
{
	uint32_t config;
	int setup_rc;
	int write_rc;
	int err;

	/* No runtime PM in this layer, so pm_device_driver_init() resumed it. */
	zassert_pm_state(LPDAC_DEV, PM_DEVICE_STATE_ACTIVE);

	zassert_ok(lpdac_setup(), "channel setup failed");
	zassert_ok(lpdac_write(LPDAC_MIDSCALE), "write failed");
	zassert_true(lpdac_output_on(), "GCR[DACEN] clear while PM reports ACTIVE");
	config = lpdac_gcr_config();

	zassert_ok(pm_device_action_run(LPDAC_DEV, PM_DEVICE_ACTION_SUSPEND),
		   "SUSPEND action rejected");
	zassert_pm_state(LPDAC_DEV, PM_DEVICE_STATE_SUSPENDED);
	zassert_false(lpdac_output_on(), "GCR[DACEN] still set after SUSPEND");
	zassert_equal(lpdac_gcr_config(), config,
		      "SUSPEND changed the configuration: 0x%08x -> 0x%08x", config,
		      lpdac_gcr_config());

	/*
	 * The API contract while the device is not active. Returning an error is the
	 * whole point: the alternative is writing GCR and DATA behind PM's back, which
	 * either drives a pin PM believes it has quietened or is thrown away by the
	 * next power cycle, and the caller is told neither.
	 */
	write_rc = lpdac_write(LPDAC_MIDSCALE);
	setup_rc = lpdac_setup();
	zassert_equal(write_rc, -EBUSY, "write against a SUSPENDED device returned %d, want -EBUSY",
		      write_rc);
	zassert_equal(setup_rc, -EBUSY,
		      "channel setup against a SUSPENDED device returned %d, want -EBUSY",
		      setup_rc);
	zassert_false(lpdac_output_on(), "a refused call re-enabled the output buffer");

	zassert_ok(pm_device_action_run(LPDAC_DEV, PM_DEVICE_ACTION_RESUME),
		   "RESUME action rejected");
	zassert_pm_state(LPDAC_DEV, PM_DEVICE_STATE_ACTIVE);
	lpdac_report_hw("after-resume");

	/* The output the consumer last asked for, and the configuration it was made
	 * with, both come back without the consumer doing anything.
	 */
	zassert_true(lpdac_output_on(), "RESUME did not put the output buffer back");
	zassert_equal(lpdac_gcr_config(), config,
		      "RESUME left a different configuration: 0x%08x, want 0x%08x",
		      lpdac_gcr_config(), config);

	/* A redundant action must be refused, not obeyed and not fatal. */
	err = pm_device_action_run(LPDAC_DEV, PM_DEVICE_ACTION_RESUME);
	zassert_equal(err, -EALREADY, "second RESUME returned %d, want -EALREADY", err);
}

ZTEST(lpdac_pm, test_device_turn_on_rebuilds_the_register_block)
{
	uint32_t config;
	uint32_t config_clobbered;
	uint32_t config_after_turn_on;
	bool on_after_turn_on;
	bool on_after_resume;
	int suspend_rc;
	int turn_off_rc;
	int turn_on_rc;
	int resume_rc;

	zassert_pm_state(LPDAC_DEV, PM_DEVICE_STATE_ACTIVE);
	zassert_ok(lpdac_setup(), "channel setup failed");
	zassert_ok(lpdac_write(LPDAC_MIDSCALE), "write failed");
	config = lpdac_gcr_config();

	/* Collected in one go and asserted afterwards, so that a failure cannot hand
	 * the next test a device the core still records as OFF.
	 */
	suspend_rc = pm_device_action_run(LPDAC_DEV, PM_DEVICE_ACTION_SUSPEND);
	lpdac_gcr_clobber();
	config_clobbered = lpdac_gcr_config();

	turn_off_rc = pm_device_action_run(LPDAC_DEV, PM_DEVICE_ACTION_TURN_OFF);

	turn_on_rc = pm_device_action_run(LPDAC_DEV, PM_DEVICE_ACTION_TURN_ON);
	config_after_turn_on = lpdac_gcr_config();
	on_after_turn_on = lpdac_output_on();
	lpdac_report_hw("after-turn-on");

	resume_rc = pm_device_action_run(LPDAC_DEV, PM_DEVICE_ACTION_RESUME);
	on_after_resume = lpdac_output_on();
	lpdac_report_hw("after-resume");

	zassert_ok(suspend_rc, "SUSPEND rejected (%d)", suspend_rc);
	zassert_ok(turn_off_rc, "TURN_OFF rejected (%d)", turn_off_rc);
	zassert_ok(turn_on_rc, "TURN_ON rejected (%d)", turn_on_rc);
	zassert_ok(resume_rc, "RESUME rejected (%d)", resume_rc);
	zassert_pm_state(LPDAC_DEV, PM_DEVICE_STATE_ACTIVE);

	/* Before anything else: a clobber that did not take would make the restore
	 * assertion below pass without the driver having rebuilt a thing.
	 */
	zassert_not_equal(config_clobbered, config,
			  "the clobber did not take, so this test proves nothing");

	zassert_equal(config_after_turn_on, config,
		      "TURN_ON left 0x%08x, want the devicetree configuration 0x%08x",
		      config_after_turn_on, config);

	/*
	 * And it has to stop there. TURN_ON returns with the core about to record the
	 * device as suspended, so a block left converting would be driving a pin that
	 * PM believes is quiet; putting the output back is RESUME's job.
	 */
	zassert_false(on_after_turn_on, "TURN_ON left the output buffer on");
	zassert_true(on_after_resume, "RESUME did not put the output buffer back");
}
