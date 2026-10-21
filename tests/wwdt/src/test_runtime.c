/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The runtime layer: who holds the reference, and what a put() can do.
 *
 * The driver takes no runtime reference of its own -- a running watchdog has no
 * completion event to release one at -- so with
 * PM_DEVICE_RUNTIME_DEFAULT_ENABLE the device boots SUSPENDED with its bus
 * clock gated, and a wdt_setup() without a reference is refused rather than
 * starting a watchdog the PM core records as suspended. Both are observed from
 * the suite setup, before the reference the setup then takes for the rest of
 * the run.
 *
 * Once started, a final synchronous put fails and retains the reference and
 * ACTIVE state. The counter and its bus access must remain intact.
 */

#include <errno.h>

#include <zephyr/drivers/watchdog.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "wwdt.h"

static struct {
  enum pm_device_state boot_state;
  bool gate_at_boot;
  int install_rc;
  int setup_rc;
  bool gate_after_setup;
} unreferenced;

int wwdt_layer_before_start(void) {
  (void)pm_device_state_get(wwdt_dev, &unreferenced.boot_state);
  unreferenced.gate_at_boot = wwdt_gate_is_open();

  unreferenced.install_rc = wwdt_install();
  unreferenced.setup_rc = wdt_setup(wwdt_dev, 0);
  unreferenced.gate_after_setup = wwdt_gate_is_open();
  return 0;
}

ZTEST(wwdt_pm, test_runtime_boots_suspended_and_refuses_setup) {
  zassert_true(unreferenced.boot_state == PM_DEVICE_STATE_SUSPENDED ||
                   unreferenced.boot_state == PM_DEVICE_STATE_OFF,
               "%s: booted %s with no consumer", wwdt_dev->name,
               pm_device_state_str(unreferenced.boot_state));
  zassert_false(unreferenced.gate_at_boot,
                "%s: booted suspended with its bus clock open", wwdt_dev->name);

  zassert_ok(unreferenced.install_rc,
             "%s: install_timeout without a reference failed (%d)",
             wwdt_dev->name, unreferenced.install_rc);
  zassert_equal(unreferenced.setup_rc, -EBUSY,
                "%s: wdt_setup() without a reference returned %d",
                wwdt_dev->name, unreferenced.setup_rc);
  zassert_false(unreferenced.gate_after_setup,
                "%s: the refused wdt_setup() opened the bus clock",
                wwdt_dev->name);
}

ZTEST(wwdt_pm, test_runtime_put_cannot_stop_the_watchdog) {
  struct wwdt_image img = {0};
  bool gate_put;
  bool counting_put;
  int feed_put;
  int put_rc;

  zassert_pm_state(wwdt_dev, PM_DEVICE_STATE_ACTIVE);

  put_rc = pm_device_runtime_put(wwdt_dev);
  zassert_equal(put_rc, -ENOTSUP, "%s: final put returned %d", wwdt_dev->name,
                put_rc);
  zassert_pm_state(wwdt_dev, PM_DEVICE_STATE_ACTIVE);

  gate_put = wwdt_gate_is_open();
  if (gate_put) {
    wwdt_image_read(&img);
  }
  counting_put = gate_put && wwdt_counting();
  feed_put = gate_put ? wwdt_feed_and_verify() : -EIO;
  wwdt_report_hw("after put");

  zassert_true(gate_put, "%s: put() gated the bus clock of a running watchdog",
               wwdt_dev->name);
  zassert_true(wwdt_image_enabled(&img), "%s: MOD[WDEN] clear after put()",
               wwdt_dev->name);
  zassert_true(counting_put, "%s: the watchdog stopped counting after put()",
               wwdt_dev->name);
  zassert_ok(feed_put, "%s: a feed after put() did not reload TV (%d)",
             wwdt_dev->name, feed_put);
  zassert_pm_state(wwdt_dev, PM_DEVICE_STATE_ACTIVE);
  zassert_false(wwdt_expired(), "%s: the watchdog ran out", wwdt_dev->name);
}
