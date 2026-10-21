/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/pm/policy.h>
#include <zephyr/ztest.h>

#include <pm_test/lowpower.h>
#include <pm_test/state.h>

#include "wwdt.h"

BUILD_ASSERT(DT_NODE_HAS_PROP(WWDT_NODE, zephyr_disabling_power_states),
             "WWDT must declare its incompatible system states");

static unsigned int locks_before;

static unsigned int locked_states(void) {
  unsigned int count = 0U;

  for (enum pm_state state = PM_STATE_ACTIVE; state < PM_STATE_COUNT; state++) {
    count += pm_policy_state_lock_is_active(state, 0U) ? 1U : 0U;
  }
  return count;
}

int wwdt_layer_before_start(void) {
  pm_test_console_quiesce();
  locks_before = locked_states();
  return 0;
}

ZTEST(wwdt_pm, test_system_constraints_follow_running_lifetime) {
  unsigned int declared = 0U;

  zassert_equal(locks_before, 0U,
                "constraints already held before watchdog start");
  zassert_true(wwdt_counting());
  pm_test_console_quiesce();
  for (enum pm_state state = PM_STATE_RUNTIME_IDLE; state < PM_STATE_COUNT;
       state++) {
    bool disabling = pm_policy_device_is_disabling_state(wwdt_dev, state, 0U);

    declared += disabling ? 1U : 0U;
    zassert_equal(pm_policy_state_lock_is_active(state, 0U), disabling,
                  "unexpected constraint for state %d", state);
  }
  zassert_true(declared > 0U, "no enabled disabling states were generated");
  zassert_equal(wdt_setup(wwdt_dev, 0), -EBUSY);
  zassert_equal(pm_device_runtime_put(wwdt_dev), -ENOTSUP);
  zassert_pm_state(wwdt_dev, PM_DEVICE_STATE_ACTIVE);
  pm_test_console_quiesce();
  zassert_equal(locked_states(), declared,
                "failed calls released or changed constraints");

  const struct pm_state_info *next = pm_policy_next_state(0U, K_TICKS_FOREVER);

  zassert_true(
      next == NULL || !pm_policy_device_is_disabling_state(
                          wwdt_dev, next->state, next->substate_id),
      "policy selected a state incompatible with the running watchdog");
  zassert_ok(wwdt_feed_and_verify());
}
