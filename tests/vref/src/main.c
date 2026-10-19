/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/dt-bindings/regulator/nxp_vref.h>

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
	zassert_false(regulator_is_enabled(VREF_DEV), "previous test leaked a regulator reference");
	zassert_ok(regulator_set_mode(VREF_DEV, DT_PROP_OR(VREF_NODE, regulator_initial_mode,
							 NXP_VREF_MODE_STANDBY)));
}

ZTEST_SUITE(vref_pm, NULL, vref_pm_setup, vref_pm_before, NULL, NULL);
