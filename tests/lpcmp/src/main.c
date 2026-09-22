/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief LPCMP device-PM suite: the suite hooks, and nothing else.
 *
 * Which layer this build tests, and therefore which src/test_*.c is compiled in,
 * is decided by the pm-* snippet on the command line. See docs/pm-layers.md.
 */

#include <zephyr/ztest.h>

#include <pm_test/layer.h>

#include "lpcmp.h"

#if defined(CONFIG_PM_TEST_LPCMP_LOOPBACK)
static int loopback_rc = -EBUSY;
#endif

static void *lpcmp_pm_setup(void)
{
	(void)pm_test_setup();
#if defined(CONFIG_PM_TEST_LPCMP_LOOPBACK)
	loopback_rc = lpcmp_loopback_init();
#endif
	return NULL;
}

static void lpcmp_pm_before(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_true(device_is_ready(LPCMP_DEV), "%s is not ready", LPCMP_DEV->name);
#if defined(CONFIG_PM_TEST_LPCMP_LOOPBACK)
	/* Reported from before() because a setup() that fails has no way to fail the
	 * suite. The jumper is the usual reason, so name it rather than the errno.
	 */
	zassert_ok(loopback_rc,
		   "loopback GPIO unusable (%d); the layer needs a jumper, see README.md",
		   loopback_rc);
#endif
}

ZTEST_SUITE(lpcmp_pm, NULL, lpcmp_pm_setup, lpcmp_pm_before, NULL, NULL);
