/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The device layer: the driver's SUSPEND/RESUME callbacks, driven directly.
 *
 * Two tests. The round trip is one ordered sequence, because it starts from the
 * state the device booted in. The second is a defect this layer is the only one
 * that can see: an API call that re-enables the hardware behind PM's back.
 */

#include <zephyr/drivers/comparator.h>
#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "lpcmp.h"

ZTEST(lpcmp_pm, test_device_suspend_resume)
{
	uint32_t ccr2_before;
	int err;

	/* No runtime PM in this layer, so pm_device_driver_init() resumed it. */
	zassert_pm_state(LPCMP_DEV, PM_DEVICE_STATE_ACTIVE);
	zassert_true(lpcmp_cmp_en(), "CCR0.CMP_EN clear while PM reports ACTIVE");

	ccr2_before = lpcmp_ccr2();

	zassert_ok(pm_device_action_run(LPCMP_DEV, PM_DEVICE_ACTION_SUSPEND),
		   "SUSPEND action rejected");
	zassert_pm_state(LPCMP_DEV, PM_DEVICE_STATE_SUSPENDED);
	zassert_false(lpcmp_cmp_en(), "CCR0.CMP_EN still set after SUSPEND");

	/* Not an error path: the API reads CSR.COUT, so a suspended comparator
	 * yields a stale level. Worth having in the log; nothing to assert on.
	 */
	TC_PRINT("read while suspended: output=%d (stale CSR.COUT)\n", lpcmp_output());

#if defined(CONFIG_PM_TEST_LPCMP_LOOPBACK)
	zassert_false(lpcmp_loopback_tracks_input(),
		      "a SUSPENDED comparator still tracks its input");
#endif

	zassert_ok(pm_device_action_run(LPCMP_DEV, PM_DEVICE_ACTION_RESUME),
		   "RESUME action rejected");
	zassert_pm_state(LPCMP_DEV, PM_DEVICE_STATE_ACTIVE);
	zassert_true(lpcmp_cmp_en(), "CCR0.CMP_EN clear after RESUME");

	zassert_equal(lpcmp_ccr2(), ccr2_before,
		      "CCR2 changed across the round trip: 0x%08x -> 0x%08x", ccr2_before,
		      lpcmp_ccr2());

#if defined(CONFIG_PM_TEST_LPCMP_LOOPBACK)
	zassert_true(lpcmp_loopback_tracks_input(), "output does not track the input after RESUME");
#endif

	/* A redundant action must be refused, not obeyed and not fatal. */
	err = pm_device_action_run(LPCMP_DEV, PM_DEVICE_ACTION_RESUME);
	zassert_equal(err, -EALREADY, "second RESUME returned %d, want -EALREADY", err);

	/*
	 * TURN_ON/TURN_OFF are deliberately not exercised. The comparator is not in a
	 * power domain here, so the only caller would be this test, and pm_device
	 * forces the state to OFF even when the driver refuses the action -- which
	 * would leave every later test in the suite running against an OFF device and
	 * report a harness artefact as a driver defect.
	 */
}

ZTEST(lpcmp_pm, test_device_api_does_not_reenable_while_suspended)
{
	int cb_rc;
	bool reenabled;

	zassert_ok(pm_device_action_run(LPCMP_DEV, PM_DEVICE_ACTION_SUSPEND),
		   "SUSPEND action rejected");
	zassert_pm_state(LPCMP_DEV, PM_DEVICE_STATE_SUSPENDED);

	/*
	 * nxp_lpcmp_set_trigger_callback() clears CMP_EN, updates the callback, then
	 * sets CMP_EN again without consulting the PM state, so it powers the
	 * comparator back up while PM still reports SUSPENDED. Expected to fail until
	 * the driver is fixed -- lpcmp-set-trigger-callback-reenables in
	 * docs/findings.md.
	 */
	cb_rc = comparator_set_trigger_callback(LPCMP_DEV, NULL, NULL);
	reenabled = lpcmp_cmp_en();

	/* Restored before the assertion below, so that a failure here cannot hand the
	 * next test a suspended comparator and be reported twice.
	 */
	zassert_ok(pm_device_action_run(LPCMP_DEV, PM_DEVICE_ACTION_RESUME),
		   "RESUME action rejected");

	TC_PRINT("set_trigger_callback while suspended returned %d\n", cb_rc);
	zassert_false(reenabled, "set_trigger_callback re-enabled a SUSPENDED comparator");
}
