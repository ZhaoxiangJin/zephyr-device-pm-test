/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief PORT pin-mux device-PM suite: the suite hooks, and nothing else.
 *
 * Which layer this build tests, and therefore which src/test_*.c is compiled in,
 * is decided by the pm-* snippet on the command line. See docs/pm-layers.md.
 */

#include <zephyr/ztest.h>

#include <pm_test/layer.h>

#include "port.h"

static void *port_pm_setup(void)
{
	(void)pm_test_setup();

	TC_PRINT("%u pin-mux instance(s) under test\n", (unsigned)PORT_CASE_COUNT);
	for (size_t i = 0U; i < PORT_CASE_COUNT; i++) {
		TC_PRINT("%s = PORT%u\n", port_cases[i].dev->name, port_cases[i].index);
	}

	return NULL;
}

static void port_pm_before(void *fixture)
{
	ARG_UNUSED(fixture);

	for (size_t i = 0U; i < PORT_CASE_COUNT; i++) {
		zassert_true(device_is_ready(port_cases[i].dev), "%s is not ready",
			     port_cases[i].dev->name);
	}
}

ZTEST_SUITE(port_pm, NULL, port_pm_setup, port_pm_before, NULL, NULL);
