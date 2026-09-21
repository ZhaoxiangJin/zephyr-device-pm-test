/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The control: this case really does drive the peripheral.
 *
 * Compiled into every layer, not just the baseline one. If this fails, no verdict
 * from the layer under test means anything -- which is the whole reason the
 * baseline layer exists as a layer.
 */

#include <zephyr/ztest.h>
#include <zephyr/pm/device_runtime.h>

#include "lpadc.h"

ZTEST(lpadc_pm, test_control_read)
{
	int32_t raw = 0;
	bool held = false;
	int rc;

	/*
	 * Where runtime PM owns the device it boots suspended, so the control has
	 * to ask for it explicitly. Reading without a reference is a claim about
	 * the driver -- that its read path holds one for the length of the
	 * sequence -- and test_runtime.c is where that claim is judged. Making the
	 * control depend on it too would mean one driver defect failed every case
	 * in the layer, which is the opposite of what a control is for.
	 */
	if (pm_device_runtime_is_enabled(LPADC_DEV)) {
		zassert_ok(pm_device_runtime_get(LPADC_DEV),
			   "could not resume the device for the control read");
		held = true;
	}

	rc = lpadc_read(&raw);

	if (held) {
		/* Before the assert below: a failed read must not leak the reference. */
		(void)pm_device_runtime_put(LPADC_DEV);
	}

	zassert_ok(rc, "control conversion did not complete");
	TC_PRINT("control raw=%d\n", raw);
}
