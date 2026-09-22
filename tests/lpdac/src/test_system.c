/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The system layer: the per-device power state constraint.
 *
 * The DAC node lists the states that stop the output in
 * zephyr,disabling-power-states (see constraints.overlay), and the driver takes no
 * policy lock of its own -- an output is wanted for as long as the application
 * wants a voltage on the pin, not for the length of one call, so there is no
 * window for the driver to protect. Holding the constraint is therefore the
 * application's job, and both tests here are written the way an application has to
 * write it: the policy lock and the runtime reference as a pair.
 */

#include <zephyr/pm/device_runtime.h>
#include <zephyr/pm/policy.h>
#include <zephyr/pm/state.h>
#include <zephyr/ztest.h>

#include <pm_test/lowpower.h>

#include "lpdac.h"

ZTEST(lpdac_pm, test_system_constraint_covers_the_declared_states)
{
	unsigned int declared = 0;

	pm_test_console_quiesce();

	for (enum pm_state state = PM_STATE_ACTIVE; state < PM_STATE_COUNT; state++) {
		zassert_false(pm_policy_state_lock_is_active(state, 0),
			      "power state %d was already locked before the test", state);
	}

	pm_policy_device_power_lock_get(LPDAC_DEV);

	for (enum pm_state state = PM_STATE_ACTIVE; state < PM_STATE_COUNT; state++) {
		bool disabling = pm_policy_device_is_disabling_state(LPDAC_DEV, state, 0);

		declared += disabling ? 1U : 0U;
		zassert_equal(pm_policy_state_lock_is_active(state, 0), disabling,
			      "power state %d: locked=%d but declared disabling=%d", state,
			      (int)pm_policy_state_lock_is_active(state, 0), (int)disabling);
	}

	/*
	 * Without this the loop above would compare "nothing locked" against "nothing
	 * declared" and pass while proving nothing -- which is exactly what a
	 * constraints.overlay that failed to reach the DAC node looks like. States that
	 * are listed but disabled in the devicetree are dropped from the generated
	 * table, so on a board where all of them are disabled this is the check that
	 * fires.
	 */
	zassert_true(declared > 0,
		     "no disabling power state reached the constraint table; is "
		     "constraints.overlay applied to this board's DAC node?");

	pm_policy_device_power_lock_put(LPDAC_DEV);

	pm_test_console_quiesce();
	for (enum pm_state state = PM_STATE_ACTIVE; state < PM_STATE_COUNT; state++) {
		zassert_false(pm_policy_state_lock_is_active(state, 0),
			      "power state %d is still locked after the constraint was "
			      "released", state);
	}
}

ZTEST(lpdac_pm, test_system_output_usable_under_the_constraint)
{
	int setup_rc;
	int write_rc;
	bool on;

	/* The pair an application needs: the reference keeps the block powered and
	 * converting, the constraint keeps the policy from picking a state that would
	 * stop it.
	 */
	pm_policy_device_power_lock_get(LPDAC_DEV);
	zassert_ok(pm_device_runtime_get(LPDAC_DEV), "runtime_get rejected");

	setup_rc = lpdac_setup();
	write_rc = lpdac_write(LPDAC_MIDSCALE);
	on = lpdac_output_on();

	/* Released before anything is asserted: a failure must not leave a lock behind,
	 * which would silently block those states for the rest of the run.
	 */
	(void)pm_device_runtime_put(LPDAC_DEV);
	pm_policy_device_power_lock_put(LPDAC_DEV);

	zassert_ok(setup_rc, "channel setup under the constraint failed (%d)", setup_rc);
	zassert_ok(write_rc, "write under the constraint failed (%d)", write_rc);
	zassert_true(on, "GCR[DACEN] clear while a runtime reference was held");
}
