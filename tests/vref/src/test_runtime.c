/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The runtime layer: the output lives outside the reference count.
 *
 * The regulator API takes no runtime PM reference of its own, so a consumer that
 * enabled the output never asked PM for anything -- and the driver's SUSPEND is a
 * no-op precisely so that the output keeps running while PM calls the device
 * suspended. That is the claim of this layer.
 *
 * It is also the layer that pays for the power domain. The vref node names
 * &core_domain, which is runtime enabled too and is suspended right after its own
 * init, so pm_device_is_powered() is already false when the regulator initialises:
 * pm_device_driver_init() skips TURN_ON, the block never receives its devicetree
 * configuration, and a later runtime get does not repair it -- the domain forwards
 * TURN_ON only on the system state changes it lists. Both configuration assertions
 * below are expected to fail until that is fixed:
 * vref-unconfigured-under-runtime-pm and pd-soc-state-change-no-turn-on-under-runtime-pm
 * in docs/findings.md.
 *
 * test_runtime_boots_unresumed has to run first: it reads the state the device
 * booted in, and the reference-counting test below leaves it SUSPENDED.
 */

#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "vref.h"

ZTEST(vref_pm, test_runtime_boots_unresumed)
{
	enum pm_device_state state;

	pm_test_note_state(VREF_DEV, "at boot");
	zassert_true(pm_device_runtime_is_enabled(VREF_DEV),
		     "runtime PM is not enabled on %s, so this layer tests nothing",
		     VREF_DEV->name);

	/*
	 * Two boot states are legal. Without a power domain the device lands in
	 * SUSPENDED: pm_device_driver_init() ran TURN_ON and stopped short of RESUME
	 * because runtime PM is about to take over. With a domain it lands in OFF,
	 * because TURN_ON was skipped. On these SoCs it is always the second.
	 */
	zassert_ok(pm_device_state_get(VREF_DEV, &state), "pm_device_state_get() failed");
	zassert_true((state == PM_DEVICE_STATE_SUSPENDED) || (state == PM_DEVICE_STATE_OFF),
		     "%s booted %s, want SUSPENDED or OFF under runtime PM", VREF_DEV->name,
		     pm_device_state_str(state));

	zassert_true(vref_config_bits_present(),
		     "the block did not receive its devicetree configuration (CSR 0x%08x)",
		     vref_csr());
}

ZTEST(vref_pm, test_runtime_output_outlives_the_reference)
{
	bool stable_while_suspended;
	bool stable_after_put;
	bool configured_after_cycle;

	zassert_ok(regulator_enable(VREF_DEV), "regulator_enable() failed");

	/* A consumer holds the output while PM still calls the device suspended or
	 * off. Nothing in the regulator API resumed it, and nothing had to.
	 */
	stable_while_suspended = vref_output_stable();

	zassert_ok(pm_device_runtime_get(VREF_DEV), "pm_device_runtime_get() failed");
	zassert_pm_state(VREF_DEV, PM_DEVICE_STATE_ACTIVE);

	zassert_ok(pm_device_runtime_put(VREF_DEV), "pm_device_runtime_put() failed");
	zassert_pm_state(VREF_DEV, PM_DEVICE_STATE_SUSPENDED);

	stable_after_put = vref_output_stable();
	configured_after_cycle = vref_config_bits_present();
	vref_report_hw("after-put");

	/* Released before the assertions, so that a failure cannot hand the next test
	 * an enabled reference it did not ask for.
	 */
	zassert_ok(regulator_disable(VREF_DEV), "regulator_disable() failed");

	zassert_true(stable_while_suspended, "output not stable while the device is not ACTIVE");
	zassert_true(stable_after_put, "the driver's SUSPEND dropped the consumer's output");
	zassert_true(configured_after_cycle,
		     "still unconfigured after a runtime get/put cycle (CSR 0x%08x)", vref_csr());
}
