/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The runtime layer: pm_device_runtime_get()/put() reference counting.
 *
 * Two tests. The first is one ordered sequence, because what it claims starts from
 * the state the device booted in. The second only needs the device suspended,
 * which is true both at boot and after the first test's last put().
 */

#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "lpcmp.h"

ZTEST(lpcmp_pm, test_runtime_boots_suspended)
{
	/* With runtime PM, pm_device_driver_init() leaves the device SUSPENDED. */
	zassert_pm_state(LPCMP_DEV, PM_DEVICE_STATE_SUSPENDED);

	/*
	 * And the hardware has to agree. nxp_lpcmp_init() sets CCR0.CMP_EN before
	 * calling pm_device_driver_init(), which records SUSPENDED without running the
	 * SUSPEND callback, so the comparator is left comparing and drawing analog
	 * current that PM believes it has already saved. Expected to fail until the
	 * driver is fixed -- lpcmp-boots-enabled-while-suspended in docs/findings.md.
	 */
	zassert_false(lpcmp_cmp_en(), "CCR0.CMP_EN set at init while PM reports SUSPENDED");
}

ZTEST(lpcmp_pm, test_runtime_reference_counting)
{
	/*
	 * An unwrapped read is recorded, not asserted. The comparator API has no way
	 * to report a suspended device -- get_output() returns the CSR.COUT latch --
	 * and this driver takes no reference of its own, so the call neither resumes
	 * the device nor fails. There is no contract here to hold it to.
	 */
	TC_PRINT("unwrapped output=%d (stale CSR.COUT, no reference taken)\n", lpcmp_output());

	zassert_ok(pm_device_runtime_get(LPCMP_DEV), "runtime_get rejected");
	zassert_pm_state(LPCMP_DEV, PM_DEVICE_STATE_ACTIVE);
	zassert_true(lpcmp_cmp_en(), "CCR0.CMP_EN clear while PM reports ACTIVE");

	/* A second reference must not be a second resume, and releasing it must not
	 * suspend a device somebody else is still holding.
	 */
	zassert_ok(pm_device_runtime_get(LPCMP_DEV), "nested runtime_get rejected");
	zassert_ok(pm_device_runtime_put(LPCMP_DEV), "first runtime_put rejected");
	zassert_pm_state(LPCMP_DEV, PM_DEVICE_STATE_ACTIVE);
	zassert_true(lpcmp_cmp_en(), "CCR0.CMP_EN cleared while a reference was still held");

	zassert_ok(pm_device_runtime_put(LPCMP_DEV), "last runtime_put rejected");
	zassert_pm_state(LPCMP_DEV, PM_DEVICE_STATE_SUSPENDED);
	zassert_false(lpcmp_cmp_en(), "CCR0.CMP_EN still set after the last runtime_put");
}
