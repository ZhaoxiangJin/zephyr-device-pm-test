/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The runtime layer: pm_device_runtime_get()/put() reference counting.
 *
 * Two tests. The first is one ordered sequence because it starts from the state
 * the device booted in, which nothing else may have disturbed. The second stands
 * on its own: it only needs the device suspended, which is true both at boot and
 * after the first test's put().
 */

#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "lpadc.h"

/*
 * The reference the read path takes is dropped with pm_device_runtime_put_async()
 * from the completion interrupt, so the suspend itself runs later on the system
 * work queue. Let it run before sampling the state.
 */
static void settle_async_suspend(void)
{
	k_msleep(20);
}

ZTEST(lpadc_pm, test_runtime_reference_counting)
{
	int32_t raw = 0;

	/* With runtime PM, pm_device_driver_init() leaves the device SUSPENDED. */
	zassert_pm_state(LPADC_DEV, PM_DEVICE_STATE_SUSPENDED);

	/*
	 * The read path holds a reference for the length of the sequence, so an
	 * unwrapped read resumes the converter, converts, and lets it go again.
	 */
	zassert_ok(lpadc_read(&raw), "unwrapped read did not auto-resume");
	TC_PRINT("unwrapped raw=%d\n", raw);

	settle_async_suspend();
	zassert_pm_state(LPADC_DEV, PM_DEVICE_STATE_SUSPENDED);

	/*
	 * A caller holding its own reference across several reads keeps the
	 * converter up, and the reads must not disturb that reference.
	 */
	zassert_ok(pm_device_runtime_get(LPADC_DEV), "runtime_get rejected");
	zassert_pm_state(LPADC_DEV, PM_DEVICE_STATE_ACTIVE);

	zassert_ok(lpadc_read(&raw), "read under a held reference failed");
	TC_PRINT("wrapped raw=%d\n", raw);

	settle_async_suspend();
	zassert_pm_state(LPADC_DEV, PM_DEVICE_STATE_ACTIVE);

	zassert_ok(pm_device_runtime_put(LPADC_DEV), "runtime_put rejected");
	zassert_pm_state(LPADC_DEV, PM_DEVICE_STATE_SUSPENDED);
}

ZTEST(lpadc_pm, test_runtime_channel_setup_while_suspended)
{
	int32_t raw = 0;

	zassert_pm_state(LPADC_DEV, PM_DEVICE_STATE_SUSPENDED);

	/*
	 * channel_setup() must not touch the reference supplies while the device is
	 * suspended: doing so leaves the regulator's use count high for good -- see
	 * lpadc-channel-setup-unbalances-bandgap in docs/findings.md. All it may do
	 * is record what the next RESUME has to apply.
	 */
	zassert_ok(lpadc_channels_setup(), "channel setup against a SUSPENDED device failed");
	zassert_pm_state(LPADC_DEV, PM_DEVICE_STATE_SUSPENDED);

	/* And what it recorded has to actually be applied on the way back up. */
	zassert_ok(lpadc_read(&raw), "read after a suspended channel setup failed");
	TC_PRINT("post-setup raw=%d\n", raw);

	settle_async_suspend();
}
