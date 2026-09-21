/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief LPADC device-PM suite: the suite hooks, and nothing else.
 *
 * Which layer this build tests, and therefore which src/test_*.c is compiled in,
 * is decided by the pm-* snippet on the command line. See docs/pm-layers.md.
 */

#include <zephyr/ztest.h>

#include <pm_test/layer.h>

#include "lpadc.h"

/*
 * Channel setup happens once, in setup(), rather than before every test: the
 * reference supplies a channel setup touches are use-counted, and repeating it
 * would be testing that rather than the layer.
 */
static int setup_rc = -EBUSY;

static void *lpadc_pm_setup(void)
{
	(void)pm_test_setup();
	setup_rc = lpadc_channels_setup();
	return NULL;
}

static void lpadc_pm_before(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Reported from before() because a setup() that fails has no way to fail
	 * the suite -- this way every test in the run says why it is meaningless.
	 */
	zassert_ok(setup_rc, "LPADC channel setup failed (%d)", setup_rc);
}

ZTEST_SUITE(lpadc_pm, NULL, lpadc_pm_setup, lpadc_pm_before, NULL, NULL);
