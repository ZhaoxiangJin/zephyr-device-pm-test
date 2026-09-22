/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief LPDAC device-PM suite: the suite hooks, and nothing else.
 *
 * Which layer this build tests, and therefore which src/test_*.c is compiled in,
 * is decided by the pm-* snippet on the command line. See docs/pm-layers.md.
 */

#include <zephyr/ztest.h>

#include <pm_test/layer.h>
#include <pm_test/state.h>

#include "lpdac.h"

static void *lpdac_pm_setup(void)
{
	(void)pm_test_setup();
	lpdac_report_configuration();
	pm_test_note_state(LPDAC_DEV, "at boot");
	lpdac_report_hw("at boot");
	return NULL;
}

static void lpdac_pm_before(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_true(device_is_ready(LPDAC_DEV), "%s is not ready", LPDAC_DEV->name);
}

ZTEST_SUITE(lpdac_pm, NULL, lpdac_pm_setup, lpdac_pm_before, NULL, NULL);
