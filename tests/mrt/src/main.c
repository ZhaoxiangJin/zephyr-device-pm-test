/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief MRT device-PM suite: the suite hooks, and nothing else.
 *
 * Which layer this build tests, and therefore which src/test_*.c is compiled in, is
 * decided by the pm-* snippet on the command line. See docs/pm-layers.md.
 */

#include <zephyr/ztest.h>

#include <pm_test/layer.h>
#include <pm_test/state.h>

#include "mrt.h"

static void *mrt_pm_setup(void)
{
	(void)pm_test_setup();
	mrt_report_configuration();

	MRT_FOREACH_CASE(c) {
		pm_test_note_state(c->dev, "at boot");
		mrt_report_hw(c, "at boot");
	}

	/* The module carries the clock and the power domain, so its boot state is as
	 * much a part of the picture as the channels'.
	 */
	pm_test_note_state(mrt_cases[0].module, "module at boot");

	return NULL;
}

static void mrt_pm_before(void *fixture)
{
	ARG_UNUSED(fixture);

	MRT_FOREACH_CASE(c) {
		zassert_true(device_is_ready(c->dev), "%s is not ready", c->dev->name);
		zassert_true(device_is_ready(c->module), "%s is not ready", c->module->name);
	}
}

ZTEST_SUITE(mrt_pm, NULL, mrt_pm_setup, mrt_pm_before, NULL, NULL);
