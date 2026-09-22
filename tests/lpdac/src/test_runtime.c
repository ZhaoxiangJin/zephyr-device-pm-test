/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The runtime layer: the consumer owns the output, and says so with a
 *        reference.
 *
 * This driver takes no runtime PM reference of its own. A DAC output is held for
 * as long as the application wants a voltage on the pin, and there is no
 * completion event a put() could hang on, so wrapping each API call in its own
 * get/put would drop the output the moment the call returned. The reference is the
 * consumer's, and a call that arrives without one is refused.
 *
 * Two tests. The first has to run first: it reads the state the device booted in,
 * and the second leaves it suspended.
 */

#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "lpdac.h"

ZTEST(lpdac_pm, test_runtime_boots_without_a_consumer)
{
	enum pm_device_state state;
	int setup_rc;

	zassert_true(pm_device_runtime_is_enabled(LPDAC_DEV),
		     "runtime PM is not enabled on %s, so this layer tests nothing",
		     LPDAC_DEV->name);

	/*
	 * Two boot states are legal. Without a power domain the device lands in
	 * SUSPENDED: pm_device_driver_init() ran TURN_ON and stopped short of RESUME
	 * because runtime PM is about to take over. With a domain it lands in OFF,
	 * because the domain is runtime-suspended before the DAC initialises and
	 * TURN_ON was skipped -- pd-soc-state-change-no-turn-on-under-runtime-pm in
	 * docs/findings.md. On these SoCs it is always the second, and it costs this
	 * driver nothing: the configuration is written by channel_setup(), which a
	 * consumer cannot call without first taking a reference.
	 */
	zassert_ok(pm_device_state_get(LPDAC_DEV, &state), "pm_device_state_get() failed");
	zassert_true((state == PM_DEVICE_STATE_SUSPENDED) || (state == PM_DEVICE_STATE_OFF),
		     "%s booted %s, want SUSPENDED or OFF under runtime PM", LPDAC_DEV->name,
		     pm_device_state_str(state));

	/* Nothing may be driving a pin before a consumer has asked for it. */
	zassert_false(lpdac_output_on(), "GCR[DACEN] set before any consumer asked for an output");

	setup_rc = lpdac_setup();
	zassert_equal(setup_rc, -EBUSY, "channel setup without a reference returned %d, want -EBUSY",
		      setup_rc);
	zassert_false(lpdac_output_on(), "a refused channel setup enabled the output buffer");
}

ZTEST(lpdac_pm, test_runtime_reference_counting)
{
	uint32_t config;

	zassert_ok(pm_device_runtime_get(LPDAC_DEV), "runtime_get rejected");
	zassert_pm_state(LPDAC_DEV, PM_DEVICE_STATE_ACTIVE);

	zassert_ok(lpdac_setup(), "channel setup under a held reference failed");
	zassert_ok(lpdac_write(LPDAC_MIDSCALE), "write under a held reference failed");
	zassert_true(lpdac_output_on(), "GCR[DACEN] clear while PM reports ACTIVE");
	config = lpdac_gcr_config();

	/* A second reference must not be a second resume, and releasing it must not
	 * drop an output somebody else is still holding.
	 */
	zassert_ok(pm_device_runtime_get(LPDAC_DEV), "nested runtime_get rejected");
	zassert_ok(pm_device_runtime_put(LPDAC_DEV), "first runtime_put rejected");
	zassert_pm_state(LPDAC_DEV, PM_DEVICE_STATE_ACTIVE);
	zassert_true(lpdac_output_on(), "output buffer switched off while a reference was held");

	zassert_ok(pm_device_runtime_put(LPDAC_DEV), "last runtime_put rejected");
	zassert_pm_state(LPDAC_DEV, PM_DEVICE_STATE_SUSPENDED);
	lpdac_report_hw("after-put");
	zassert_false(lpdac_output_on(), "GCR[DACEN] still set after the last runtime_put");

	/*
	 * And the consumer gets its output back by taking the reference again, without
	 * calling channel_setup() a second time: what it configured is the driver's to
	 * remember across a suspend it did not ask for.
	 */
	zassert_ok(pm_device_runtime_get(LPDAC_DEV), "runtime_get after the put rejected");
	lpdac_report_hw("after-reget");
	zassert_true(lpdac_output_on(), "the output did not come back with the reference");
	zassert_equal(lpdac_gcr_config(), config,
		      "configuration changed across the suspend: 0x%08x, want 0x%08x",
		      lpdac_gcr_config(), config);

	/* Left as it was found, so this test's output does not outlive it. */
	zassert_ok(pm_device_runtime_put(LPDAC_DEV), "final runtime_put rejected");
}
