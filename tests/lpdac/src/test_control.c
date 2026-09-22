/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The control: this case really does drive the DAC.
 *
 * Compiled into every layer. If this fails, no verdict from the layer under test
 * means anything -- which is the whole reason the baseline layer exists as a layer.
 */

#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#include "lpdac.h"

ZTEST(lpdac_pm, test_control_write)
{
	bool held = false;
	uint32_t config;
	bool on;
	int setup_rc;
	int write_rc;

	/*
	 * Where runtime PM owns the device it boots suspended, and this driver takes
	 * no reference of its own: the consumer owns the output's lifetime, because a
	 * DAC output has no completion event a put() could hang on. So the control has
	 * to ask for the device explicitly. That an unwrapped call is refused instead
	 * of being served is test_runtime.c's claim, not a premise of the control.
	 */
	if (pm_device_runtime_is_enabled(LPDAC_DEV)) {
		zassert_ok(pm_device_runtime_get(LPDAC_DEV),
			   "could not resume the device for the control write");
		held = true;
	}

	setup_rc = lpdac_setup();
	write_rc = lpdac_write(LPDAC_MIDSCALE);
	config = lpdac_gcr_config();
	on = lpdac_output_on();
	lpdac_report_hw("control");

	if (held) {
		/* Before the assertions below: a failure must not leak the reference. */
		(void)pm_device_runtime_put(LPDAC_DEV);
	}

	zassert_ok(setup_rc, "dac_channel_setup() failed (%d)", setup_rc);
	zassert_ok(write_rc, "dac_write_value(%u) failed (%d)", (unsigned int)LPDAC_MIDSCALE,
		   write_rc);
	zassert_true(on, "GCR[DACEN] clear after a write, so the output buffer is off");

	/*
	 * The rest of GCR is what the driver wrote from the devicetree. A cleared
	 * register is what a reset leaves, and every later layer tells "restored" from
	 * "came back from reset" by comparing against this, so a case in which it is
	 * empty proves nothing anywhere.
	 */
	zassert_not_equal(config, 0U, "GCR carries no configuration after channel setup");
}
