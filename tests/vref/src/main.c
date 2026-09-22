/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief VREF device-PM suite: the suite hooks, and nothing else.
 *
 * Which layer this build tests, and therefore which src/test_*.c is compiled in,
 * is decided by the pm-* snippet on the command line. See docs/pm-layers.md.
 */

#include <zephyr/ztest.h>

#include <pm_test/layer.h>

#include "vref.h"

static void *vref_pm_setup(void)
{
	(void)pm_test_setup();
	vref_report_configuration();
	vref_report_hw("at boot");
	return NULL;
}

static void vref_pm_before(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_true(device_is_ready(VREF_DEV), "%s is not ready", VREF_DEV->name);
}

ZTEST_SUITE(vref_pm, NULL, vref_pm_setup, vref_pm_before, NULL, NULL);
