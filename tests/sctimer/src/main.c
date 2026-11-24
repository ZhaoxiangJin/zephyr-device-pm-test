/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief SCTimer device-PM suite: the suite hooks, and nothing else.
 *
 * Which layer this build tests, and therefore which src/test_*.c is compiled in,
 * is decided by the pm-* snippet on the command line. See docs/pm-layers.md.
 */

#include <zephyr/ztest.h>
#include <zephyr/pm/device_runtime.h>

#include <pm_test/layer.h>
#include <pm_test/state.h>

#include "sctimer.h"

#if defined(CONFIG_PM_TEST_LAYER_RUNTIME)
static int runtime_setup_rc;
#endif

static void *sctimer_pm_setup(void)
{
	(void)pm_test_setup();
#if defined(CONFIG_PM_TEST_LAYER_RUNTIME)
	runtime_setup_rc = sctimer_runtime_snapshot_boot();
	if (runtime_setup_rc != 0) {
		TC_PRINT("runtime setup failed: %d\n", runtime_setup_rc);
		return NULL;
	}
#endif
	sctimer_report_configuration();

	SCTIMER_FOREACH_CASE(c) {
		pm_test_note_state(c->dev, "at boot");
		sctimer_report_hw(c, "at boot");
	}

	return NULL;
}

static void sctimer_pm_before(void *fixture)
{
	ARG_UNUSED(fixture);

#if defined(CONFIG_PM_TEST_LAYER_RUNTIME)
	zassert_ok(runtime_setup_rc, "runtime setup failed (%d)", runtime_setup_rc);
#endif

	SCTIMER_FOREACH_CASE(c) {
		zassert_true(device_is_ready(c->dev), "%s is not ready", c->dev->name);
	}
}

static void sctimer_pm_after(void *fixture)
{
	ARG_UNUSED(fixture);

	SCTIMER_FOREACH_CASE(c) {
		if (!device_is_ready(c->dev)) {
			continue;
		}
		if (pm_device_runtime_is_enabled(c->dev)) {
			int usage = pm_device_runtime_usage(c->dev);

			/* The tests take at most two nested consumer references. */
			for (int count = 0; count < usage && count < 2; count++) {
				zassert_ok(pm_device_runtime_put(c->dev),
					   "%s: reference cleanup failed", c->dev->name);
			}
			zassert_equal(usage, 0, "%s: test leaked %d reference(s)",
				      c->dev->name, usage);
		}
#if defined(CONFIG_PM_DEVICE)
		else {
			enum pm_device_state state;

			zassert_ok(pm_device_state_get(c->dev, &state));
			if (state == PM_DEVICE_STATE_OFF) {
				zassert_ok(pm_device_action_run(c->dev, PM_DEVICE_ACTION_TURN_ON));
				state = PM_DEVICE_STATE_SUSPENDED;
			}
			if (state == PM_DEVICE_STATE_SUSPENDED) {
				zassert_ok(pm_device_action_run(c->dev, PM_DEVICE_ACTION_RESUME));
			}
		}
#endif
	}
}

static void sctimer_pm_teardown(void *fixture)
{
	ARG_UNUSED(fixture);

#if defined(CONFIG_PM_TEST_LAYER_RUNTIME)
	sctimer_runtime_release_console();
#endif
}

ZTEST_SUITE(sctimer_pm, NULL, sctimer_pm_setup, sctimer_pm_before, sctimer_pm_after,
	    sctimer_pm_teardown);
