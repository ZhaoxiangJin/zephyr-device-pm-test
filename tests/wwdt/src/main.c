/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief WWDT device-PM suite: the suite hooks, and nothing else.
 *
 * Which layer this build tests, and therefore which src/test_*.c is compiled
 * in, is decided by the pm-* snippet on the command line. See
 * docs/pm-layers.md.
 *
 * Idle observations precede the shared running session. The terminal disable
 * suite runs afterward, without relying on test registration or shuffle order.
 */

#include <errno.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/pm/policy.h>
#include <zephyr/ztest.h>

#include <pm_test/layer.h>
#include <pm_test/lowpower.h>
#include <pm_test/state.h>

#include "wwdt.h"

enum wwdt_test_phase {
  WWDT_TEST_RUNNING,
  WWDT_TEST_STOPPED,
};

static bool wwdt_running_phase(const void *state) {
  return state != NULL && *(const enum wwdt_test_phase *)state == WWDT_TEST_RUNNING;
}

static bool wwdt_stopped_phase(const void *state) {
  return state != NULL && *(const enum wwdt_test_phase *)state == WWDT_TEST_STOPPED;
}

static void *wwdt_pm_setup(void) {
  (void)pm_test_setup();
  wwdt_report_configuration();

  pm_test_note_state(wwdt_dev, "at boot");
  wwdt_report_hw("at boot");

  if (!device_is_ready(wwdt_dev)) {
    wwdt_start_rc = -ENODEV;
    return NULL;
  }

  TC_PRINT("pre-start checks begin\n");
  wwdt_start_rc = wwdt_layer_before_start();
  TC_PRINT("pre-start checks returned %d\n", wwdt_start_rc);
  if (wwdt_start_rc != 0) {
    TC_PRINT("pre-start checks failed: %d\n", wwdt_start_rc);
    return NULL;
  }

  wwdt_start_rc = wwdt_install();
  if (wwdt_start_rc != 0) {
    return NULL;
  }
  const uint8_t unsupported[] = {
      WDT_OPT_PAUSE_IN_SLEEP,
      WDT_OPT_PAUSE_HALTED_BY_DBG,
      BIT(7),
  };
  bool gate_before = wwdt_gate_is_open();

  for (size_t i = 0U; i < ARRAY_SIZE(unsupported); i++) {
    int ret = wdt_setup(wwdt_dev, unsupported[i]);

    if ((ret != -ENOTSUP) || (wwdt_gate_is_open() != gate_before)) {
      TC_PRINT("unsupported option 0x%x: result %d, gate %u -> %u\n",
               unsupported[i], ret, gate_before, wwdt_gate_is_open());
      wwdt_start_rc = -EIO;
      return NULL;
    }
  }

#if defined(CONFIG_PM_DEVICE_RUNTIME)
  /*
   * The driver takes no runtime reference of its own: the consumer holds one
   * for as long as the watchdog runs, until the terminal disable suite.
   */
  TC_PRINT("runtime get begin\n");
  wwdt_start_rc = pm_device_runtime_get(wwdt_dev);
  TC_PRINT("runtime get returned %d\n", wwdt_start_rc);
  if (wwdt_start_rc != 0) {
    TC_PRINT("watchdog resume failed: %d\n", wwdt_start_rc);
    return NULL;
  }
#endif

  TC_PRINT("watchdog start begin\n");
  wwdt_start_rc = wwdt_start();
  TC_PRINT("watchdog start returned %d\n", wwdt_start_rc);
  if (wwdt_start_rc != 0) {
    int ret = pm_device_runtime_put(wwdt_dev);

    if (ret != 0) {
      TC_PRINT("runtime reference cleanup failed: %d\n", ret);
    }
    return NULL;
  }
  wwdt_report_hw("started");

  return NULL;
}

static void wwdt_pm_before(void *fixture) {
  ARG_UNUSED(fixture);

  zassert_true(device_is_ready(wwdt_dev), "%s is not ready", wwdt_dev->name);
  zassert_ok(wwdt_start_rc, "%s: the watchdog did not start (%d)",
             wwdt_dev->name, wwdt_start_rc);

  /* Every test begins a full period away from the timeout. */
  zassert_ok(wwdt_feed_and_verify(), "%s: feed reload not observed",
             wwdt_dev->name);
}

static void wwdt_pm_after(void *fixture) {
  ARG_UNUSED(fixture);

  zassert_ok(wwdt_feed_and_verify(), "feed reload after test not observed");
}

ZTEST_SUITE(wwdt_pm, wwdt_running_phase, wwdt_pm_setup, wwdt_pm_before,
            wwdt_pm_after, NULL);

ZTEST(wwdt_stop, test_disable_stops_and_uninstalls) {
  const struct wdt_timeout_cfg cfg = {
      .window = {.min = 0U, .max = WWDT_TIMEOUT_MS},
      .flags = WDT_FLAG_RESET_NONE,
  };
  uint32_t stopped_tv;

  zassert_ok(wwdt_start_rc);
  zassert_ok(wdt_disable(wwdt_dev));
  zassert_true(wwdt_clock_is_halted());
  k_busy_wait(2000);
  stopped_tv = wwdt_tv();
  k_busy_wait(2U * WWDT_TIMEOUT_MS * 1000U);
  zassert_equal(wwdt_tv(), stopped_tv, "disabled counter moved");
  zassert_false(wwdt_expired(), "disabled timeout fired");
  zassert_equal(wdt_disable(wwdt_dev), -EFAULT);
  zassert_equal(wdt_feed(wwdt_dev, 0), -EINVAL);
  zassert_equal(wdt_install_timeout(wwdt_dev, &cfg), -ENOTSUP);
  zassert_equal(wdt_setup(wwdt_dev, 0), -ENOTSUP);

#if defined(CONFIG_PM_POLICY_DEVICE_CONSTRAINTS)
  pm_test_console_quiesce();
  for (enum pm_state state = PM_STATE_RUNTIME_IDLE; state < PM_STATE_COUNT;
       state++) {
    if (pm_policy_device_is_disabling_state(wwdt_dev, state, 0U)) {
      zassert_false(pm_policy_state_lock_is_active(state, 0U),
                    "disable retained constraint for state %d", state);
    }
  }
#endif

#if defined(CONFIG_PM_DEVICE_RUNTIME)
  zassert_ok(pm_device_runtime_put(wwdt_dev));
  zassert_equal(pm_device_runtime_usage(wwdt_dev), 0);
  zassert_ok(pm_device_runtime_get(wwdt_dev));
#elif defined(CONFIG_PM_DEVICE)
  zassert_ok(pm_device_action_run(wwdt_dev, PM_DEVICE_ACTION_SUSPEND));
  zassert_ok(pm_device_action_run(wwdt_dev, PM_DEVICE_ACTION_RESUME));
#endif
  zassert_true(wwdt_gate_is_open());
  zassert_true(wwdt_clock_is_halted());
  zassert_equal(wwdt_tv(), stopped_tv, "PM resumed disabled counter");
  wwdt_layer_after_stop();
  zassert_equal(wdt_setup(wwdt_dev, 0), -ENOTSUP);
  zassert_equal(wdt_feed(wwdt_dev, 0), -EINVAL);

#if defined(CONFIG_PM_DEVICE_RUNTIME)
  zassert_ok(pm_device_runtime_put(wwdt_dev));
#endif
}

ZTEST_SUITE(wwdt_stop, wwdt_stopped_phase, NULL, NULL, NULL, NULL);

void test_main(void) {
  enum wwdt_test_phase phase = WWDT_TEST_RUNNING;

  ztest_run_test_suites(&phase, false, 1, 1);
  phase = WWDT_TEST_STOPPED;
  ztest_run_test_suites(&phase, false, 1, 1);
  ztest_verify_all_test_suites_ran();
}
