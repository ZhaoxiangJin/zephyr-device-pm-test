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
 * Suite setup captures the boot state before any test configures the DAC.
 * The reference-counting test leaves it suspended.
 */

#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "lpdac.h"

static struct {
	bool enabled;
	enum pm_device_state state;
	int state_rc;
	int setup_rc;
} runtime_boot;

void lpdac_capture_runtime_boot(void)
{
	runtime_boot.enabled = pm_device_runtime_is_enabled(LPDAC_DEV);
	runtime_boot.state_rc = pm_device_state_get(LPDAC_DEV, &runtime_boot.state);
	runtime_boot.setup_rc = lpdac_setup();
}

ZTEST(lpdac_pm, test_runtime_boots_without_a_consumer)
{
	zassert_true(runtime_boot.enabled,
		     "runtime PM is not enabled on %s, so this layer tests nothing",
		     LPDAC_DEV->name);

	zassert_ok(runtime_boot.state_rc, "pm_device_state_get() failed");
	zassert_true((runtime_boot.state == PM_DEVICE_STATE_SUSPENDED) ||
		     (runtime_boot.state == PM_DEVICE_STATE_OFF),
		     "%s booted %s, want SUSPENDED or OFF under runtime PM", LPDAC_DEV->name,
		     pm_device_state_str(runtime_boot.state));

	zassert_equal(runtime_boot.setup_rc, -EBUSY,
		      "channel setup without a reference returned %d, want -EBUSY",
		      runtime_boot.setup_rc);
}

ZTEST(lpdac_pm, test_runtime_reference_counting)
{
	uint32_t config;

	zassert_ok(pm_device_runtime_get(LPDAC_DEV), "runtime_get rejected");
	zassert_pm_state(LPDAC_DEV, PM_DEVICE_STATE_ACTIVE);

	zassert_ok(lpdac_setup(), "channel setup under a held reference failed");
	zassert_ok(lpdac_write(LPDAC_MIDSCALE), "write under a held reference failed");
	zassert_true(lpdac_output_on(),
		     "output enables incomplete while PM reports ACTIVE");
	config = lpdac_gcr_config();
	zassert_not_equal(config, 0U, "GCR carries no configuration after channel setup");

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
	zassert_true(lpdac_output_off(),
		     "an output enable remains set after runtime_put");
	zassert_equal(lpdac_setup(), -EBUSY, "setup without a runtime reference was accepted");
	zassert_equal(lpdac_write(LPDAC_MIDSCALE), -EBUSY,
		      "write without a runtime reference was accepted");
	zassert_pm_state(LPDAC_DEV, PM_DEVICE_STATE_SUSPENDED);
	zassert_true(lpdac_output_off(),
		     "a refused API call enabled the output");
	zassert_equal(lpdac_gcr_config(), config, "a refused API call changed configuration");

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
