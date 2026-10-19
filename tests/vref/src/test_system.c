/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/pm/device_runtime.h>
#include <zephyr/pm/policy.h>
#include <zephyr/ztest.h>

#include <pm_test/lowpower.h>

#include "vref.h"

static void assert_constraints(bool locked)
{
	unsigned int states = 0U;

	pm_test_console_quiesce();
	for (enum pm_state state = PM_STATE_RUNTIME_IDLE; state < PM_STATE_COUNT; state++) {
		if (pm_policy_device_is_disabling_state(VREF_DEV, state, 0U)) {
			states++;
			zassert_equal(pm_policy_state_lock_is_active(state, 0U), locked,
				      "state %d lock does not follow the output", state);
		}
	}
	zassert_true(states > 0U, "no enabled disabling state: this test proves nothing");
}

ZTEST(vref_pm, test_system_constraints_follow_output)
{
	assert_constraints(false);
	zassert_ok(regulator_enable(VREF_DEV));
	zassert_ok(regulator_enable(VREF_DEV));
	assert_constraints(true);
	zassert_ok(regulator_disable(VREF_DEV));
	assert_constraints(true);
	zassert_ok(regulator_disable(VREF_DEV));
	assert_constraints(false);
}

ZTEST(vref_pm, test_system_active_registers_do_not_hold_output_constraint)
{
	zassert_ok(pm_device_runtime_get(VREF_DEV));
	zassert_true(vref_output_disabled());
	assert_constraints(false);
	zassert_ok(regulator_enable(VREF_DEV));
	assert_constraints(true);
	zassert_ok(regulator_disable(VREF_DEV));
	assert_constraints(false);
	zassert_ok(pm_device_runtime_put(VREF_DEV));
}
