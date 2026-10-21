/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>
#include <zephyr/ztest.h>

#include <pm_test/lowpower.h>
#include <pm_test/state.h>

#include "wwdt.h"

static struct wwdt_image idle_before;
static struct wwdt_image idle_after;
static bool idle_gate_after;
static volatile unsigned int sleep_entries;
static volatile unsigned int transition_entries;

static void state_entry(enum pm_state state) {
  if (state == PM_STATE_RUNTIME_IDLE) {
    sleep_entries++;
  }
  if (state == PM_TEST_TRANSITION_STATE) {
    transition_entries++;
  }
}

static struct pm_notifier notifier = {
    .state_entry = state_entry,
};

int wwdt_layer_before_start(void) {
  int ret = wwdt_install();

  if (ret != 0) {
    return ret;
  }
  ret = wwdt_seed_idle_registers();
  if (ret != 0) {
    return ret;
  }
  wwdt_image_read(&idle_before);
  pm_notifier_register(&notifier);
  pm_test_enter_transition();
  idle_gate_after = wwdt_gate_is_open();
  if (idle_gate_after) {
    wwdt_image_read(&idle_after);
  }
  pm_test_console_resume(PM_TEST_CONSOLE_REINIT_PINMUX);
  wwdt_report_hw("idle after transition");
  return 0;
}

ZTEST(wwdt_pm, test_idle_transition_before_start) {
  zassert_true(transition_entries > 0U, "requested transition was not entered");
  zassert_true(idle_gate_after,
               "bus access not restored after idle transition");
  zassert_false(wwdt_image_enabled(&idle_after),
                "idle transition started watchdog");
#if defined(CONFIG_PM_TEST_LAYER_DPD)
  zassert_not_equal(idle_before.tc, 0xFFU, "missing non-reset sentinel");
  zassert_equal(idle_after.tc, 0xFFU,
                "WWDT did not lose its register state in DPD");
#else
  zassert_true(wwdt_image_equal(&idle_before, &idle_after),
               "idle retained transition changed registers");
#endif
  /* Suite setup started the same installed timeout after the idle transition.
   */
  zassert_true(wwdt_counting());
  zassert_ok(wwdt_feed_and_verify());
}

ZTEST(wwdt_pm, test_running_watchdog_allows_only_compatible_sleep) {
  struct wwdt_image before;
  struct wwdt_image after;
  uint32_t first;
  uint32_t second;
  unsigned int entries = sleep_entries;
  const struct pm_state_info sleep = {
      .state = PM_STATE_RUNTIME_IDLE,
  };

  zassert_pm_state(wwdt_dev, PM_DEVICE_STATE_ACTIVE);
  wwdt_image_read(&before);
  first = wwdt_tv();
  zassert_true(pm_state_force(0U, &sleep));
  k_sleep(K_MSEC(20));
  second = wwdt_tv();
  wwdt_image_read(&after);
  zassert_true(sleep_entries > entries, "ordinary Sleep was not entered");
  zassert_pm_state(wwdt_dev, PM_DEVICE_STATE_ACTIVE);
  zassert_true(wwdt_image_equal(&before, &after));
  zassert_true(second < first,
               "counter stopped or reloaded across ordinary Sleep");

  pm_test_console_quiesce();
  const struct pm_state_info *next = pm_policy_next_state(0U, K_TICKS_FOREVER);

  zassert_true(next == NULL || !pm_policy_device_is_disabling_state(
                                   wwdt_dev, next->state, next->substate_id),
               "normal policy selected an incompatible state");
  zassert_ok(wwdt_feed_and_verify());
  zassert_false(wwdt_expired());
}

void wwdt_layer_after_stop(void) {
  struct wwdt_image after;
  unsigned int entries = transition_entries;
  uint32_t stopped_tv = wwdt_tv();

  pm_test_enter_transition();
  pm_test_console_resume(PM_TEST_CONSOLE_REINIT_PINMUX);
  zassert_true(transition_entries > entries, "disabled transition not entered");
  zassert_true(wwdt_gate_is_open());
  wwdt_image_read(&after);
#if defined(CONFIG_PM_TEST_LAYER_DPD)
  ARG_UNUSED(stopped_tv);
  zassert_false(wwdt_image_enabled(&after));
  zassert_equal(after.tc, 0xFFU, "disabled WWDT did not lose state in DPD");
#else
  zassert_true(wwdt_clock_is_halted());
  zassert_equal(wwdt_tv(), stopped_tv, "disabled counter advanced in sleep");
#endif
  k_busy_wait(2U * WWDT_TIMEOUT_MS * 1000U);
  zassert_false(wwdt_expired(), "disabled watchdog callback after wake");
  zassert_false(wwdt_counting(), "disabled watchdog counting after wake");
}
