/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The control, compiled into every layer: this case really drives the
 * WWDT.
 *
 * A layer verdict from a build whose watchdog never ran would mean nothing, so
 * each layer first establishes that the watchdog the suite setup started is
 * enabled, counts down, takes a feed, and has not run out.
 */

#include <zephyr/ztest.h>

#include "wwdt.h"

ZTEST(wwdt_pm, test_control_watchdog_runs_and_takes_a_feed) {
  struct wwdt_image img;

  zassert_true(wwdt_gate_is_open(),
               "%s: the bus clock of a running watchdog is gated",
               wwdt_dev->name);

  wwdt_image_read(&img);
  wwdt_report_hw("control");

  zassert_true(wwdt_image_enabled(&img),
               "%s: MOD[WDEN] is clear after setup (MOD=0x%02x)",
               wwdt_dev->name, img.mod);
  /* The premise every layer leans on: a missed feed cannot reset the board. */
  zassert_false(wwdt_image_resets(&img), "%s: MOD[WDRESET] is set (MOD=0x%02x)",
                wwdt_dev->name, img.mod);
  zassert_true(wwdt_counting(), "%s: TV is not counting down", wwdt_dev->name);
  zassert_ok(wwdt_feed_and_verify(), "%s: a feed did not reload TV",
             wwdt_dev->name);
  zassert_false(wwdt_expired(), "%s: the watchdog ran out", wwdt_dev->name);
}
