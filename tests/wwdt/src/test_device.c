/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The device layer: the driver's PM callback, driven directly.
 *
 * Before the watchdog runs, an idle WWDT is all the callback can act on:
 * SUSPEND hands back its bus clock and RESUME takes it again, TURN_ON leaves
 * the clock with the RESUME that follows it, and a wdt_setup() on the suspended
 * device is refused without the clock being opened behind the PM core's back.
 *
 * A running watchdog supervises the CPU across compatible sleep states.
 * SUSPEND is refused without changing the PM state, configuration or counter.
 *
 * What this layer cannot do is hand TURN_ON a block that really came back from
 * reset: MOD[WDEN] cannot be cleared to stand in for one. That claim is the dpd
 * layer's.
 */

#include <errno.h>

#include <zephyr/drivers/watchdog.h>
#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "wwdt.h"

/* What the device looked like before the watchdog ran, recorded from the suite
 * setup. */
static struct {
  enum pm_device_state boot_state;
  bool gate_at_boot;
  int suspend_rc;
  bool gate_suspended;
  int install_rc;
  int setup_rc;
  bool gate_after_setup;
  int turn_off_rc;
  int turn_on_rc;
  bool gate_after_turn_on;
  int resume_rc;
  bool gate_resumed;
  bool enabled_resumed;
} idle;

int wwdt_layer_before_start(void) {
  struct wwdt_image img = {0};

  (void)pm_device_state_get(wwdt_dev, &idle.boot_state);
  idle.gate_at_boot = wwdt_gate_is_open();

  idle.suspend_rc = pm_device_action_run(wwdt_dev, PM_DEVICE_ACTION_SUSPEND);
  idle.gate_suspended = wwdt_gate_is_open();

  idle.install_rc = wwdt_install();
  idle.setup_rc = wdt_setup(wwdt_dev, 0);
  idle.gate_after_setup = wwdt_gate_is_open();

  idle.turn_off_rc = pm_device_action_run(wwdt_dev, PM_DEVICE_ACTION_TURN_OFF);
  idle.turn_on_rc = pm_device_action_run(wwdt_dev, PM_DEVICE_ACTION_TURN_ON);
  idle.gate_after_turn_on = wwdt_gate_is_open();

  idle.resume_rc = pm_device_action_run(wwdt_dev, PM_DEVICE_ACTION_RESUME);
  idle.gate_resumed = wwdt_gate_is_open();
  if (idle.gate_resumed) {
    wwdt_image_read(&img);
  }
  idle.enabled_resumed = wwdt_image_enabled(&img);
  return 0;
}

ZTEST(wwdt_pm, test_device_idle_watchdog_releases_its_clock) {
  zassert_equal(idle.boot_state, PM_DEVICE_STATE_ACTIVE,
                "%s: booted %s, not active", wwdt_dev->name,
                pm_device_state_str(idle.boot_state));
  zassert_true(idle.gate_at_boot,
               "%s: the bus clock is gated on an active device",
               wwdt_dev->name);

  zassert_ok(idle.suspend_rc, "%s: SUSPEND failed (%d)", wwdt_dev->name,
             idle.suspend_rc);
  zassert_false(idle.gate_suspended,
                "%s: SUSPEND left the bus clock of an idle watchdog open",
                wwdt_dev->name);

  zassert_ok(idle.install_rc,
             "%s: install_timeout on a suspended device failed (%d)",
             wwdt_dev->name, idle.install_rc);
  zassert_equal(idle.setup_rc, -EBUSY,
                "%s: wdt_setup() on a suspended device returned %d",
                wwdt_dev->name, idle.setup_rc);
  zassert_false(idle.gate_after_setup,
                "%s: the refused wdt_setup() opened the bus clock",
                wwdt_dev->name);

  zassert_ok(idle.turn_off_rc, "%s: TURN_OFF failed (%d)", wwdt_dev->name,
             idle.turn_off_rc);
  zassert_ok(idle.turn_on_rc, "%s: TURN_ON failed (%d)", wwdt_dev->name,
             idle.turn_on_rc);
  zassert_false(idle.gate_after_turn_on,
                "%s: TURN_ON left the bus clock of an idle watchdog open",
                wwdt_dev->name);

  zassert_ok(idle.resume_rc, "%s: RESUME failed (%d)", wwdt_dev->name,
             idle.resume_rc);
  zassert_true(idle.gate_resumed, "%s: RESUME did not take the bus clock back",
               wwdt_dev->name);
  zassert_false(idle.enabled_resumed,
                "%s: the watchdog runs without a wdt_setup()", wwdt_dev->name);
}

ZTEST(wwdt_pm, test_device_running_watchdog_survives_the_callback) {
  struct wwdt_image before;
  struct wwdt_image after;
  uint32_t tv_before;
  uint32_t tv_after;

  zassert_pm_state(wwdt_dev, PM_DEVICE_STATE_ACTIVE);
  k_busy_wait(5000);
  wwdt_image_read(&before);
  tv_before = wwdt_tv();

  zassert_equal(pm_device_action_run(wwdt_dev, PM_DEVICE_ACTION_SUSPEND),
                -ENOTSUP);
  zassert_equal(wdt_setup(wwdt_dev, 0), -EBUSY);
  zassert_pm_state(wwdt_dev, PM_DEVICE_STATE_ACTIVE);

  zassert_true(wwdt_gate_is_open(),
               "%s: the bus clock is gated after the round trip",
               wwdt_dev->name);
  tv_after = wwdt_tv();
  wwdt_image_read(&after);
  wwdt_report_hw("after turn-on");

  zassert_true(wwdt_image_enabled(&before),
               "%s: premise: the watchdog is not running", wwdt_dev->name);
  zassert_true(
      wwdt_image_equal(&after, &before),
      "%s: the configuration changed: MOD=0x%02x TC=0x%06x, was 0x%02x/0x%06x",
      wwdt_dev->name, after.mod, after.tc, before.mod, before.tc);
  zassert_true(tv_after <= tv_before,
               "%s: a refused operation reloaded TV (0x%06x -> "
               "0x%06x)",
               wwdt_dev->name, tv_before, tv_after);
  zassert_true(wwdt_counting(),
               "%s: the watchdog is not counting after the round trip",
               wwdt_dev->name);
  zassert_ok(wwdt_feed_and_verify(), "feed stopped reaching the watchdog");
  zassert_false(wwdt_expired(), "%s: the watchdog ran out", wwdt_dev->name);
}
