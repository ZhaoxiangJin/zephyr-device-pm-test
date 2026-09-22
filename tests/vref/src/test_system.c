/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The system layer: nothing holds a constraint on the reference's behalf.
 *
 * The driver never calls pm_policy_device_power_lock_get(), and should not: an
 * enabled reference is not an operation in flight, so there is no window to
 * protect. Declaring the state the block does not survive is still meaningful --
 * that is what an application would consult to decide whether it has to re-enable
 * the output after a transition -- but nothing may end up *holding* a lock,
 * because a reference is alive for the whole run and a leaked lock would block the
 * state for ever.
 *
 * The one state constraints.overlay names, &deeppowerdown, is opt-in in the SoC
 * devicetree and stays disabled in this layer, so it never reaches the generated
 * constraint table and pm_policy_device_is_disabling_state() has nothing to report
 * here. What the declaration is for is asked where it matters, in
 * src/test_lowpower.c under the dpd layer.
 */

#include <zephyr/pm/policy.h>
#include <zephyr/pm/state.h>
#include <zephyr/ztest.h>

#include <pm_test/lowpower.h>

#include "vref.h"

ZTEST(vref_pm, test_system_no_constraint_held_for_the_reference)
{
	zassert_ok(regulator_enable(VREF_DEV), "regulator_enable() failed");
	zassert_true(vref_output_stable(), "output does not read stable once enabled");

	pm_test_console_quiesce();

	/*
	 * Every state, not just the one constraints.overlay names: the claim is that
	 * an enabled reference costs the policy nothing, and a lock on any state would
	 * be a cost. States the devicetree does not declare simply read as unlocked.
	 */
	for (enum pm_state state = PM_STATE_ACTIVE; state < PM_STATE_COUNT; state++) {
		bool locked = pm_policy_state_lock_is_active(state, 0);

		if (locked) {
			/* Released before failing, so the next test starts clean. */
			(void)regulator_disable(VREF_DEV);
		}
		zassert_false(locked,
			      "power state %d is locked while a reference is enabled, but nothing "
			      "should hold a constraint on its behalf", state);
	}

	vref_report_hw("constraints");
	zassert_ok(regulator_disable(VREF_DEV), "regulator_disable() failed");
}
