/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The system layer: the per-device power state constraint.
 *
 * &lpcmp0 lists the states that cut the comparator's analog bias in
 * zephyr,disabling-power-states (see constraints.overlay). Unlike the LPADC driver,
 * the LPCMP driver takes no policy lock of its own -- a comparator is useful for as
 * long as the application wants its output, not for the length of one call -- so
 * holding the constraint is the application's job, and both tests here are written
 * the way an application has to write it.
 */

#include <zephyr/pm/device_runtime.h>
#include <zephyr/pm/policy.h>
#include <zephyr/pm/state.h>
#include <zephyr/ztest.h>

#include <pm_test/lowpower.h>

#include "lpcmp.h"

ZTEST(lpcmp_pm, test_system_constraint_covers_the_declared_states)
{
	unsigned int declared = 0;

	pm_test_console_quiesce();

	for (enum pm_state state = PM_STATE_ACTIVE; state < PM_STATE_COUNT; state++) {
		zassert_false(pm_policy_state_lock_is_active(state, 0),
			      "power state %d was already locked before the test", state);
	}

	pm_policy_device_power_lock_get(LPCMP_DEV);

	for (enum pm_state state = PM_STATE_ACTIVE; state < PM_STATE_COUNT; state++) {
		bool disabling = pm_policy_device_is_disabling_state(LPCMP_DEV, state, 0);

		declared += disabling ? 1U : 0U;
		zassert_equal(pm_policy_state_lock_is_active(state, 0), disabling,
			      "power state %d: locked=%d but declared disabling=%d", state,
			      (int)pm_policy_state_lock_is_active(state, 0), (int)disabling);
	}

	/*
	 * Without this the loop above would compare "nothing locked" against "nothing
	 * declared" and pass while proving nothing -- which is exactly what a
	 * constraints.overlay that failed to reach &lpcmp0 looks like. States that are
	 * listed but disabled in the devicetree are dropped from the generated table,
	 * so on a board where all of them are disabled this is the check that fires.
	 */
	zassert_true(declared > 0,
		     "no disabling power state reached the constraint table; is "
		     "constraints.overlay applied to this board's comparator node?");

	pm_policy_device_power_lock_put(LPCMP_DEV);

	pm_test_console_quiesce();
	for (enum pm_state state = PM_STATE_ACTIVE; state < PM_STATE_COUNT; state++) {
		zassert_false(pm_policy_state_lock_is_active(state, 0),
			      "power state %d is still locked after the constraint was "
			      "released", state);
	}
}

ZTEST(lpcmp_pm, test_system_comparator_usable_under_the_constraint)
{
	int out;
	bool enabled;

	/* The pair an application needs: the reference keeps the block powered, the
	 * constraint keeps the policy from picking a state that would unpower it.
	 */
	pm_policy_device_power_lock_get(LPCMP_DEV);
	zassert_ok(pm_device_runtime_get(LPCMP_DEV), "runtime_get rejected");

	out = lpcmp_output();
	enabled = lpcmp_cmp_en();

	/* Released before anything is asserted: a failure must not leave a lock behind,
	 * which would silently block those states for the rest of the run.
	 */
	(void)pm_device_runtime_put(LPCMP_DEV);
	pm_policy_device_power_lock_put(LPCMP_DEV);

	zassert_true(enabled, "CCR0.CMP_EN clear while a runtime reference was held");
	zassert_true(out == 0 || out == 1, "get_output returned %d, want a level", out);
}
