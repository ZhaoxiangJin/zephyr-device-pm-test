/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The control: this case really does drive the comparator.
 *
 * Compiled into every layer. If this fails, no verdict from the layer under test
 * means anything -- which is the whole reason the baseline layer exists as a layer.
 */

#include <zephyr/drivers/comparator.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#include "lpcmp.h"

ZTEST(lpcmp_pm, test_control_api)
{
	bool held = false;
	int out, both_edges, pending, none;
#if defined(CONFIG_PM_TEST_LPCMP_LOOPBACK)
	bool tracks;
#endif

	/* Where runtime PM owns the device it boots suspended, so the control has to
	 * ask for it explicitly; whether an unwrapped call resumes it is
	 * test_runtime.c's claim to judge, not a premise of the control.
	 */
	if (pm_device_runtime_is_enabled(LPCMP_DEV)) {
		zassert_ok(pm_device_runtime_get(LPCMP_DEV),
			   "could not resume the device for the control");
		held = true;
	}

	/* Collected before anything is asserted: an assertion here would leave the
	 * reference held for the rest of the run and every later test with it.
	 */
	out = lpcmp_output();
	both_edges = comparator_set_trigger(LPCMP_DEV, COMPARATOR_TRIGGER_BOTH_EDGES);
	pending = comparator_trigger_is_pending(LPCMP_DEV);
	none = comparator_set_trigger(LPCMP_DEV, COMPARATOR_TRIGGER_NONE);
#if defined(CONFIG_PM_TEST_LPCMP_LOOPBACK)
	tracks = lpcmp_loopback_tracks_input();
#endif
	TC_PRINT("control output=%d CCR0.CMP_EN=%u CCR2=0x%08x\n", out,
		 (unsigned)lpcmp_cmp_en(), lpcmp_ccr2());

	if (held) {
		(void)pm_device_runtime_put(LPCMP_DEV);
	}

	zassert_true(out == 0 || out == 1, "get_output returned %d, want a level", out);
	zassert_ok(both_edges, "set_trigger(BOTH_EDGES) rejected");
	zassert_true(pending >= 0, "trigger_is_pending errored (%d)", pending);
	zassert_ok(none, "set_trigger(NONE) rejected");
#if defined(CONFIG_PM_TEST_LPCMP_LOOPBACK)
	zassert_true(tracks, "output does not track the looped-back GPIO");
#endif
}
