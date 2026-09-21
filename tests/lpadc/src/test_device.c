/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The device layer: the driver's SUSPEND/RESUME callbacks, driven directly.
 *
 * One test, because the sequence is the claim: a device that is ACTIVE, then
 * suspended, then resumed. Splitting it would leave tests that only pass in the
 * order ztest happens to run them.
 */

#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "lpadc.h"

ZTEST(lpadc_pm, test_device_suspend_resume)
{
	int32_t raw = 0;
	int err;

	/* No runtime PM in this layer, so pm_device_driver_init() resumed it. */
	zassert_pm_state(LPADC_DEV, PM_DEVICE_STATE_ACTIVE);

	zassert_ok(pm_device_action_run(LPADC_DEV, PM_DEVICE_ACTION_SUSPEND),
		   "SUSPEND action rejected");
	zassert_pm_state(LPADC_DEV, PM_DEVICE_STATE_SUSPENDED);

	/*
	 * The driver's SUSPEND calls LPADC_Enable(false), so a conversion started
	 * now cannot complete. -EBUSY is the contract: rejecting the request is the
	 * only acceptable behaviour, and returning it is what the driver was fixed
	 * to do -- see lpadc-suspended-read-hangs in docs/findings.md, where it used
	 * to hang instead and took three of the six layers down with it.
	 */
	err = lpadc_read(&raw);
	zassert_equal(err, -EBUSY, "read against a SUSPENDED device returned %d, want -EBUSY", err);

	zassert_ok(pm_device_action_run(LPADC_DEV, PM_DEVICE_ACTION_RESUME),
		   "RESUME action rejected");
	zassert_pm_state(LPADC_DEV, PM_DEVICE_STATE_ACTIVE);

	zassert_ok(lpadc_read(&raw), "conversion did not complete after RESUME");
	TC_PRINT("post-resume raw=%d\n", raw);

	/* A redundant action must be refused, not obeyed and not fatal. */
	err = pm_device_action_run(LPADC_DEV, PM_DEVICE_ACTION_RESUME);
	zassert_equal(err, -EALREADY, "second RESUME returned %d, want -EALREADY", err);
}
