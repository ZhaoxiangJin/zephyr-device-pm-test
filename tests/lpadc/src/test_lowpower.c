/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The sysmanaged and dpd layers: the device sweep across one transition.
 *
 * One file for two layers, because the sequence is identical and only the state
 * being entered differs -- and that difference is what separates the two claims.
 * In sysmanaged the sweep's SUSPEND and RESUME are all that happen. In dpd the
 * peripheral actually loses power, so a RESUME that only re-enables the block is
 * not enough: the core domain hands the driver TURN_OFF and TURN_ON, and the
 * driver has to reconfigure and recalibrate a register block that came back from
 * reset. dpd is the only layer that can catch a driver with no restore path -- see
 * lpadc-no-restore-after-dpd in docs/findings.md.
 */

#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>

#include <pm_test/lowpower.h>
#include <pm_test/state.h>

#include "lpadc.h"

ZTEST(lpadc_pm, test_sweep_across_transition)
{
	int32_t raw = 0;

	/* No runtime PM in these layers, so the sweep is the only thing that will
	 * suspend the device.
	 */
	zassert_pm_state(LPADC_DEV, PM_DEVICE_STATE_ACTIVE);
	zassert_ok(lpadc_read(&raw), "conversion failed before the transition");

	pm_test_enter_transition();
	pm_test_console_resume(PM_TEST_CONSOLE_REINIT_PINMUX);
	TC_PRINT("resumed from %s\n", PM_TEST_TRANSITION_NAME);

	/*
	 * pm_resume_devices() walks the devices it suspended in init order, so the
	 * core domain's TURN_ON has already run by the time the LPADC's own RESUME
	 * does. The two together have to leave a converter that works.
	 */
	zassert_pm_state(LPADC_DEV, PM_DEVICE_STATE_ACTIVE);
	zassert_ok(lpadc_read(&raw), "conversion failed after the transition");
	TC_PRINT("post-transition raw=%d\n", raw);
}
