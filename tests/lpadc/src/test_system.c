/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The system layer: the per-conversion power state constraint.
 *
 * &lpadc0 lists the states that stop it converting in zephyr,disabling-power-states
 * (see constraints.overlay), and the driver takes a pm_policy_device_power_lock
 * around each conversion. Both halves of that pair are observable through the
 * policy API, so both are checked: nothing held before the conversion, and nothing
 * left behind after it. A leaked lock would block those states for the rest of the
 * run, which is invisible until something measures the power.
 */

#include <zephyr/pm/policy.h>
#include <zephyr/ztest.h>

#include <pm_test/lowpower.h>

#include "lpadc.h"

ZTEST(lpadc_pm, test_system_constraint_is_taken_and_released)
{
	int32_t raw = 0;

	pm_test_console_quiesce();
	zassert_false(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES),
		      "a constraint was already held before the conversion");

	zassert_ok(lpadc_read(&raw), "constrained conversion did not complete");
	TC_PRINT("constrained raw=%d\n", raw);

	pm_test_console_quiesce();
	zassert_false(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES),
		      "the conversion's constraint was not released");
}
