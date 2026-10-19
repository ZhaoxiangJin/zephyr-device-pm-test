/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/dt-bindings/regulator/nxp_vref.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "vref.h"

ZTEST(vref_pm, test_runtime_output_lifetime)
{
	zassert_true(pm_device_runtime_is_enabled(VREF_DEV));
	zassert_pm_state(VREF_DEV, PM_DEVICE_STATE_SUSPENDED);
	zassert_equal(pm_device_runtime_usage(VREF_DEV), 0);
	zassert_true(vref_output_disabled());

	zassert_ok(regulator_enable(VREF_DEV));
	zassert_ok(regulator_enable(VREF_DEV));
	zassert_pm_state(VREF_DEV, PM_DEVICE_STATE_ACTIVE);
	zassert_equal(pm_device_runtime_usage(VREF_DEV), 1);
	zassert_true(vref_config_bits_present());
	zassert_true(vref_output_stable());

	zassert_ok(regulator_disable(VREF_DEV));
	zassert_pm_state(VREF_DEV, PM_DEVICE_STATE_ACTIVE);
	zassert_true(vref_output_stable());
	zassert_ok(regulator_disable(VREF_DEV));
	zassert_pm_state(VREF_DEV, PM_DEVICE_STATE_SUSPENDED);
	zassert_equal(pm_device_runtime_usage(VREF_DEV), 0);
	zassert_true(vref_output_disabled());
}

ZTEST(vref_pm, test_runtime_configuration_without_output)
{
	int32_t chosen;
	int32_t voltage;
	regulator_mode_t mode;

	zassert_ok(vref_pick_voltage(&chosen));
	zassert_ok(regulator_set_voltage(VREF_DEV, chosen, chosen));
	zassert_ok(regulator_set_mode(VREF_DEV, NXP_VREF_MODE_LOW_POWER));
	zassert_ok(regulator_get_voltage(VREF_DEV, &voltage));
	zassert_ok(regulator_get_mode(VREF_DEV, &mode));
	zassert_equal(voltage, chosen);
	zassert_equal(mode, NXP_VREF_MODE_LOW_POWER);
	zassert_pm_state(VREF_DEV, PM_DEVICE_STATE_SUSPENDED);
	zassert_equal(pm_device_runtime_usage(VREF_DEV), 0);
	zassert_true(vref_output_disabled());

	zassert_ok(regulator_enable(VREF_DEV));
	zassert_equal(vref_hw_mode(), NXP_VREF_MODE_LOW_POWER);
	zassert_ok(regulator_disable(VREF_DEV));
}

ZTEST(vref_pm, test_runtime_external_reference)
{
	zassert_ok(pm_device_runtime_get(VREF_DEV));
	zassert_ok(regulator_enable(VREF_DEV));
	zassert_equal(pm_device_runtime_usage(VREF_DEV), 2);
	zassert_ok(regulator_disable(VREF_DEV));
	zassert_pm_state(VREF_DEV, PM_DEVICE_STATE_ACTIVE);
	zassert_true(vref_output_disabled());
	zassert_ok(pm_device_runtime_put(VREF_DEV));
	zassert_pm_state(VREF_DEV, PM_DEVICE_STATE_SUSPENDED);
	zassert_equal(pm_device_runtime_usage(VREF_DEV), 0);
}
