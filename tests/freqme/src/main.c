/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief FREQME device-PM suite: the suite hooks, and nothing else.
 *
 * Which layer this build tests, and therefore which src/test_*.c is compiled in,
 * is decided by the pm-* snippet on the command line. See docs/pm-layers.md.
 */

#include <zephyr/ztest.h>

#include <pm_test/layer.h>
#include <pm_test/state.h>

#include "freqme.h"

static void *freqme_pm_setup(void)
{
	(void)pm_test_setup();
	freqme_enable_source_clocks();
	freqme_report_configuration();

	/* Nothing is read out of the block here: it has not been configured yet. */
	pm_test_note_state(freqme_dev(), "at boot");

	return NULL;
}

static void freqme_pm_before(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_true(device_is_ready(freqme_dev()), "%s is not ready", freqme_dev()->name);
}

ZTEST_SUITE(freqme_pm, NULL, freqme_pm_setup, freqme_pm_before, NULL, NULL);
