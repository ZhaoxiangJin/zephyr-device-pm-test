/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/dt-bindings/regulator/nxp_vref.h>
#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "vref.h"

static void cycle(bool enabled, regulator_mode_t mode)
{
	int32_t voltage;
	int32_t restored;
	uint32_t clobber;
	uint32_t expected_trim;

	zassert_ok(vref_pick_voltage(&voltage));
	zassert_ok(regulator_set_voltage(VREF_DEV, voltage, voltage));
	expected_trim = vref_trim();
	zassert_ok(regulator_set_mode(VREF_DEV, mode));
	if (enabled) {
		zassert_ok(regulator_enable(VREF_DEV));
		zassert_ok(regulator_enable(VREF_DEV));
	}

	for (int i = 0; i < 2; i++) {
		zassert_ok(pm_device_action_run(VREF_DEV, PM_DEVICE_ACTION_SUSPEND));
		zassert_pm_state(VREF_DEV, PM_DEVICE_STATE_SUSPENDED);
		zassert_true(vref_output_disabled(), "SUSPEND left output circuitry enabled");
		zassert_ok(pm_device_action_run(VREF_DEV, PM_DEVICE_ACTION_TURN_OFF));

		vref_config_bits_clobber();
		clobber = vref_trim_clobber();
		zassert_false(vref_config_bits_present(), "configuration corruption did not take");
		zassert_equal(vref_trim(), clobber);
		zassert_not_equal(clobber, expected_trim,
				  "trim corruption left the selected value");

		zassert_ok(pm_device_action_run(VREF_DEV, PM_DEVICE_ACTION_TURN_ON));
		zassert_pm_state(VREF_DEV, PM_DEVICE_STATE_SUSPENDED);
		zassert_true(vref_config_bits_present(), "TURN_ON did not rebuild configuration");
		zassert_true(vref_output_disabled(), "TURN_ON started an output");
		zassert_ok(pm_device_action_run(VREF_DEV, PM_DEVICE_ACTION_RESUME));
		zassert_pm_state(VREF_DEV, PM_DEVICE_STATE_ACTIVE);
		zassert_ok(regulator_get_voltage(VREF_DEV, &restored));
		zassert_equal(restored, voltage);
		zassert_equal(regulator_is_enabled(VREF_DEV), enabled);
		if (enabled) {
			zassert_true(vref_output_stable(), "held output not restored");
			zassert_equal(vref_hw_mode(), mode, "consumer mode not restored");
		} else {
			zassert_true(vref_output_disabled(), "unused output was enabled");
		}
		vref_report_hw("replayed");
	}

	if (enabled) {
		zassert_ok(regulator_disable(VREF_DEV));
		zassert_true(vref_output_stable(),
			     "first disable dropped another consumer's output");
		zassert_ok(regulator_disable(VREF_DEV));
	}
	zassert_true(vref_output_disabled());
}

ZTEST(vref_pm, test_device_held_output_replay)
{
	cycle(true, NXP_VREF_MODE_LOW_POWER);
	cycle(true, NXP_VREF_MODE_HIGH_POWER);
	cycle(true, NXP_VREF_MODE_STANDBY);
}

ZTEST(vref_pm, test_device_disabled_output_replay)
{
	cycle(false, NXP_VREF_MODE_HIGH_POWER);
}

ZTEST(vref_pm, test_device_suspended_access)
{
	int32_t voltage;
	regulator_mode_t mode;

	zassert_ok(vref_pick_voltage(&voltage));
	zassert_ok(pm_device_action_run(VREF_DEV, PM_DEVICE_ACTION_SUSPEND));
	zassert_equal(regulator_set_mode(VREF_DEV, UINT8_MAX), -EINVAL);
	zassert_equal(regulator_set_voltage(VREF_DEV, 0, 0), -EINVAL);
	zassert_equal(regulator_enable(VREF_DEV), -EBUSY);
	zassert_equal(regulator_set_mode(VREF_DEV, NXP_VREF_MODE_LOW_POWER), -EBUSY);
	zassert_equal(regulator_set_voltage(VREF_DEV, voltage, voltage), -EBUSY);
	zassert_equal(regulator_get_voltage(VREF_DEV, &voltage), -EBUSY);
	zassert_equal(regulator_get_mode(VREF_DEV, &mode), -EBUSY);
	zassert_true(vref_output_disabled());
	zassert_ok(pm_device_action_run(VREF_DEV, PM_DEVICE_ACTION_RESUME));
}
