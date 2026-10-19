/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/dt-bindings/regulator/nxp_vref.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#include <pm_test/lowpower.h>
#include <pm_test/state.h>

#include "vref.h"

static atomic_t entries;
static atomic_t exits;

static void state_entry(enum pm_state state)
{
	if (state == PM_TEST_TRANSITION_STATE) {
		atomic_inc(&entries);
	}
}

static void state_exit(enum pm_state state)
{
	if (state == PM_TEST_TRANSITION_STATE) {
		atomic_inc(&exits);
	}
}

static struct pm_notifier notifier = {
	.state_entry = state_entry,
	.state_exit = state_exit,
};

static void transition(bool enabled)
{
	int32_t before_uv;
	int32_t after_uv = 0;
	bool configured;
	bool stable;
	bool disabled;
	bool enabled_after;
	regulator_mode_t mode;
	int voltage_rc;
	int disable_rc = 0;

	zassert_ok(vref_pick_voltage(&before_uv));
	zassert_ok(regulator_set_voltage(VREF_DEV, before_uv, before_uv));
	zassert_ok(regulator_set_mode(VREF_DEV, NXP_VREF_MODE_LOW_POWER));
	if (enabled) {
		zassert_ok(regulator_enable(VREF_DEV));
		zassert_true(vref_output_stable());
	}
	zassert_pm_state(VREF_DEV, PM_DEVICE_STATE_ACTIVE);
	atomic_clear(&entries);
	atomic_clear(&exits);
	pm_notifier_register(&notifier);
	pm_test_enter_transition();

	configured = vref_config_bits_present();
	stable = vref_output_stable();
	disabled = vref_output_disabled();
	mode = vref_hw_mode();
	enabled_after = regulator_is_enabled(VREF_DEV);
	voltage_rc = regulator_get_voltage(VREF_DEV, &after_uv);

	pm_test_console_resume(PM_TEST_CONSOLE_REINIT_PINMUX);
	zassert_ok(pm_notifier_unregister(&notifier));
	vref_report_hw("resumed");
	if (enabled) {
		disable_rc = regulator_disable(VREF_DEV);
	}

	zassert_true(atomic_get(&entries) > 0, "requested power state was never entered");
	zassert_equal(atomic_get(&exits), atomic_get(&entries), "unpaired PM notifications");
	zassert_pm_state(VREF_DEV, PM_DEVICE_STATE_ACTIVE);
	zassert_true(configured, "DT configuration was not restored");
	zassert_ok(voltage_rc);
	zassert_equal(after_uv, before_uv, "consumer voltage was not restored");
	zassert_equal(enabled_after, enabled, "regulator reference count changed across PM");
	zassert_ok(disable_rc);
	if (enabled) {
		zassert_true(stable, "held output was not restored");
		zassert_equal(mode, NXP_VREF_MODE_LOW_POWER, "consumer mode was not restored");
	} else {
		zassert_true(disabled, "unused output was enabled on resume");
	}
	zassert_true(vref_output_disabled());
}

ZTEST(vref_pm, test_sweep_held_output)
{
	transition(true);
}

ZTEST(vref_pm, test_sweep_disabled_output)
{
	transition(false);
}
