/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The runtime layer: the consumer owns the channel, and says so with a
 *        reference.
 *
 * This driver takes no runtime PM reference of its own. A counter runs for as long as
 * the application wants a time base, and there is no completion event a put() could
 * hang on, so wrapping each API call in its own get/put would stop the counter the
 * moment the call returned. The reference is the consumer's.
 *
 * What this layer catches is the boot state, and then the one thing the MRT adds to
 * every counter before it: the reference is taken on a channel, while the clock it
 * needs belongs to the module. Each channel names the module as its power domain, so
 * pm_device_runtime_get() on a channel claims the module first and the PM core counts
 * the claims. The gate must therefore open for the first channel and close only after
 * the last one lets go -- and a consumer releasing one channel must not stop a
 * sibling.
 *
 * Three tests. The first has to run first: it reads the state the devices booted in,
 * and the others leave them suspended.
 */

#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "mrt.h"

ZTEST(mrt_pm, test_runtime_boots_without_a_consumer)
{
	const struct device *module = mrt_cases[0].module;
	enum pm_device_state state;

	MRT_FOREACH_CASE(c) {
		zassert_true(pm_device_runtime_is_enabled(c->dev),
			     "%s: runtime PM is not enabled on this channel", c->dev->name);

		/*
		 * Two boot states are legal. Without a power domain the channel lands in
		 * SUSPENDED; with one it lands in OFF, because the domain is
		 * runtime-suspended before the channel initialises and TURN_ON was
		 * skipped -- pd-soc-state-change-no-turn-on-under-runtime-pm in
		 * docs/findings.md. Every MRT channel names its module as its domain, so
		 * OFF is the expected one here.
		 */
		zassert_ok(pm_device_state_get(c->dev, &state),
			   "%s: pm_device_state_get() failed", c->dev->name);
		zassert_true((state == PM_DEVICE_STATE_SUSPENDED) ||
				     (state == PM_DEVICE_STATE_OFF),
			     "%s booted %s, want SUSPENDED or OFF under runtime PM",
			     c->dev->name, pm_device_state_str(state));
	}

	/*
	 * The module must be runtime-enabled too. Only its resume action turns the
	 * clock on, so a build that leaves it unmanaged while the channels are managed
	 * would gate the clock at init with nothing able to re-open it.
	 */
	zassert_true(pm_device_runtime_is_enabled(module),
		     "%s: runtime PM is not enabled on the module, so nothing can turn its "
		     "clock back on", module->name);

	if (!mrt_gate_observable()) {
		TC_PRINT("clock gate not observable, nothing asserted\n");
		return;
	}

	/* The claim. Nothing may be clocked before a consumer has asked for it. */
	zassert_false(mrt_gate_is_open(),
		      "clock gate open before any consumer took a reference");
}

ZTEST(mrt_pm, test_runtime_reference_counting)
{
	MRT_FOREACH_CASE(c) {
		zassert_ok(pm_device_runtime_get(c->dev), "%s: runtime_get rejected",
			   c->dev->name);
		zassert_pm_state(c->dev, PM_DEVICE_STATE_ACTIVE);

		if (mrt_gate_observable()) {
			zassert_true(mrt_gate_is_open(),
				     "%s: gate closed while the channel reports ACTIVE",
				     c->dev->name);
		}

		zassert_ok(mrt_start(c), "%s: could not start the channel under a held "
			   "reference", c->dev->name);
		zassert_true(mrt_wait_ticks(c, 2U, 500U), "%s: no callbacks under a held "
			     "reference", c->dev->name);

		/* A second reference must not be a second resume, and releasing it must
		 * not stop a counter somebody else is still holding.
		 */
		zassert_ok(pm_device_runtime_get(c->dev), "%s: nested runtime_get rejected",
			   c->dev->name);
		zassert_ok(pm_device_runtime_put(c->dev), "%s: first runtime_put rejected",
			   c->dev->name);
		zassert_pm_state(c->dev, PM_DEVICE_STATE_ACTIVE);
		zassert_true(mrt_advances(c),
			     "%s: the channel stopped while a reference was held", c->dev->name);

		(void)mrt_stop(c);
		zassert_ok(pm_device_runtime_put(c->dev), "%s: last runtime_put rejected",
			   c->dev->name);
		zassert_pm_state(c->dev, PM_DEVICE_STATE_SUSPENDED);
		mrt_report_hw(c, "after-put");
	}

	if (mrt_gate_observable()) {
		zassert_false(mrt_gate_is_open(),
			      "clock gate still open after every channel released");
	}
}

ZTEST(mrt_pm, test_runtime_one_channel_does_not_gate_another)
{
	const struct mrt_case *first = &mrt_cases[0];
	const struct mrt_case *second;

	if (MRT_CASE_COUNT < 2) {
		TC_PRINT("only one channel is enabled, so no sibling to protect\n");
		ztest_test_skip();
	}

	second = &mrt_cases[1];

	zassert_ok(pm_device_runtime_get(first->dev), "%s: runtime_get rejected",
		   first->dev->name);
	zassert_ok(pm_device_runtime_get(second->dev), "%s: runtime_get rejected",
		   second->dev->name);

	zassert_ok(mrt_start(first), "%s: could not start", first->dev->name);
	zassert_ok(mrt_start(second), "%s: could not start", second->dev->name);
	zassert_true(mrt_wait_ticks(first, 2U, 500U), "%s: no callbacks", first->dev->name);
	zassert_true(mrt_wait_ticks(second, 2U, 500U), "%s: no callbacks", second->dev->name);

	/*
	 * The claim of this test, and the one thing this IP adds to the counter case
	 * before it: the two channels share one clock gate, so letting go of one must
	 * leave the other running. The PM core's count on the module is what makes that
	 * true, and it is only there because each channel names the module as its
	 * power domain.
	 */
	(void)mrt_stop(second);
	zassert_ok(pm_device_runtime_put(second->dev), "%s: runtime_put rejected",
		   second->dev->name);
	zassert_pm_state(second->dev, PM_DEVICE_STATE_SUSPENDED);
	zassert_pm_state(first->dev, PM_DEVICE_STATE_ACTIVE);
	mrt_report_hw(first, "sibling-released");

	if (mrt_gate_observable()) {
		zassert_true(mrt_gate_is_open(),
			     "releasing %s gated the clock while %s was still running",
			     second->dev->name, first->dev->name);
	}

	zassert_true(mrt_advances(first), "%s: stopped when %s was released",
		     first->dev->name, second->dev->name);
	zassert_true(mrt_wait_ticks(first, 2U, 500U), "%s: callbacks stopped when %s was "
		     "released", first->dev->name, second->dev->name);

	/* Left as it was found. */
	(void)mrt_stop(first);
	zassert_ok(pm_device_runtime_put(first->dev), "%s: final runtime_put rejected",
		   first->dev->name);

	if (mrt_gate_observable()) {
		zassert_false(mrt_gate_is_open(), "gate still open after the last release");
	}
}
