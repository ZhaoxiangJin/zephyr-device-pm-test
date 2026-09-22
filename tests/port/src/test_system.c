/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The system layer: nothing holds a constraint on the pin-mux's behalf.
 *
 * The pin-mux driver never calls pm_policy_device_power_lock_get(), and should not:
 * a pad configuration is not an operation in flight, so there is no window to
 * protect. Declaring the state the block does not survive is still meaningful --
 * that is what an application would consult to decide whether it has to re-apply pin
 * state -- but nothing may end up *holding* a lock, because a pin-mux is alive for
 * the whole run and a leaked lock here would block the state for ever.
 */

#include <zephyr/pm/policy.h>
#include <zephyr/pm/state.h>
#include <zephyr/ztest.h>

#include <pm_test/lowpower.h>

#include "port.h"

ZTEST(port_pm, test_system_no_constraint_held_for_the_pinmux)
{
	pm_test_console_quiesce();

	/*
	 * Every state, not just the one constraints.overlay names: the claim is that
	 * the pin-mux costs the policy nothing, and a lock on any state would be a
	 * cost. States the devicetree does not declare simply read as unlocked.
	 */
	for (enum pm_state state = PM_STATE_ACTIVE; state < PM_STATE_COUNT; state++) {
		zassert_false(pm_policy_state_lock_is_active(state, 0),
			      "power state %d is locked, but nothing should hold a constraint on "
			      "the pin-mux's behalf", state);
	}

	for (size_t i = 0U; i < PORT_CASE_COUNT; i++) {
		port_report_gate(&port_cases[i], "constraints");
	}
}
