/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief CTIMER device-PM suite: the suite hooks, and nothing else.
 *
 * Which layer this build tests, and therefore which src/test_*.c is compiled in,
 * is decided by the pm-* snippet on the command line. See docs/pm-layers.md.
 */

#include <zephyr/ztest.h>

#include <pm_test/layer.h>
#include <pm_test/state.h>

#include "ctimer.h"

static void *ctimer_pm_setup(void)
{
	(void)pm_test_setup();
	ctimer_report_configuration();

	CTIMER_FOREACH_CASE(c) {
		pm_test_note_state(c->dev, "at boot");
		ctimer_report_hw(c, "at boot");
	}

	return NULL;
}

static void ctimer_pm_before(void *fixture)
{
	ARG_UNUSED(fixture);

	CTIMER_FOREACH_CASE(c) {
		zassert_true(device_is_ready(c->dev), "%s is not ready", c->dev->name);
	}
}

ZTEST_SUITE(ctimer_pm, NULL, ctimer_pm_setup, ctimer_pm_before, NULL, NULL);
