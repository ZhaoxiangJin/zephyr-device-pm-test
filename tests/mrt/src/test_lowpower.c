/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The sysmanaged and dpd layers: the device sweep across one transition.
 *
 * One file for two layers, because the sequence is identical and only the state being
 * entered differs. In sysmanaged the sweep's SUSPEND and RESUME are all that happen,
 * so the block keeps its power: re-opening the clock gate hands back the period the
 * consumer programmed and the channel carries on. In dpd the CORE domain is power
 * gated: the whole register block comes back at its reset value, the SYSCON clock
 * gate with it, and nothing re-runs driver init -- so the module's TURN_ON has to
 * rebuild the block and re-connect the shared interrupt, while the run the consumer
 * had is gone.
 *
 * Why the dpd assertion is behavioural and not a register comparison. INTVAL is both
 * the period register and the start trigger, so there is no way for TURN_ON to
 * restore a period without restarting a run nobody asked for, and the driver does not
 * try: a channel comes back idle. What is checked instead is that a channel started
 * from scratch afterwards delivers callbacks, which it cannot unless the bring-up ran
 * again and the interrupt is connected.
 *
 * The callback count is the control in both layers. A register image that came back
 * perfectly while the clock did not would look restored and still never interrupt.
 */

#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>

#include <pm_test/lowpower.h>
#include <pm_test/state.h>

#include "mrt.h"

ZTEST(mrt_pm, test_sweep_across_transition)
{
	struct mrt_image before[MRT_CASE_COUNT];
	struct mrt_image after[MRT_CASE_COUNT];
	uint32_t ticks_before[MRT_CASE_COUNT];
	bool gate_after;

	/* No runtime PM in these layers, so the sweep is the only thing that will
	 * suspend anything.
	 */
	zassert_pm_state(mrt_cases[0].module, PM_DEVICE_STATE_ACTIVE);

	MRT_FOREACH_CASE(c) {
		uint8_t i = (uint8_t)(c - &mrt_cases[0]);

		zassert_pm_state(c->dev, PM_DEVICE_STATE_ACTIVE);
		zassert_ok(mrt_start(c), "%s: could not start before the transition",
			   c->dev->name);
		zassert_true(mrt_wait_ticks(c, 2U, 500U),
			     "%s: no callbacks before the transition", c->dev->name);
		mrt_image_read(c, &before[i]);
		ticks_before[i] = mrt_ticks(c);
		mrt_report_hw(c, "before");
	}

	pm_test_enter_transition();

	/* Sampled before the console is revived: reviving it takes time, and the
	 * question is what the block looked like the moment the sweep handed it back.
	 */
	gate_after = mrt_gate_is_open();
	if (gate_after) {
		MRT_FOREACH_CASE(c) {
			mrt_image_read(c, &after[c - &mrt_cases[0]]);
		}
	}

	pm_test_console_resume(PM_TEST_CONSOLE_REINIT_PINMUX);
	TC_PRINT("resumed from %s\n", PM_TEST_TRANSITION_NAME);

	zassert_pm_state(mrt_cases[0].module, PM_DEVICE_STATE_ACTIVE);

	if (!mrt_gate_observable()) {
		TC_PRINT("clock gate not observable, register claims skipped\n");
	} else {
		/*
		 * The gate first: without it the register comparison below would be
		 * reading an unclocked block, and "restored" would be whatever the bus
		 * returns.
		 */
		zassert_true(gate_after,
			     "the clock gate was not re-opened across the transition");
	}

	MRT_FOREACH_CASE(c) {
		uint8_t i = (uint8_t)(c - &mrt_cases[0]);

		zassert_pm_state(c->dev, PM_DEVICE_STATE_ACTIVE);
		mrt_report_hw(c, "after-resume");

#if defined(CONFIG_PM_TEST_LAYER_DPD)
		if (mrt_gate_observable() && gate_after) {
			zassert_false(mrt_image_equal(&after[i], &before[i]),
				      "%s: the programmed period came back across %s, so the "
				      "block kept its power", c->dev->name,
				      PM_TEST_TRANSITION_NAME);
		}

		/*
		 * Power was cut, so the channel is back at reset: idle, with INTVAL
		 * clear. Starting from scratch cannot succeed unless the module's
		 * TURN_ON rebuilt the block and re-connected the interrupt.
		 */
		zassert_false(mrt_running(c),
			      "%s: the channel is running after %s without being started",
			      c->dev->name, PM_TEST_TRANSITION_NAME);
		zassert_ok(mrt_start(c), "%s: the channel does not start after %s",
			   c->dev->name, PM_TEST_TRANSITION_NAME);
		zassert_true(mrt_wait_ticks(c, 2U, 1000U),
			     "%s: a channel started after %s delivered no callbacks",
			     c->dev->name, PM_TEST_TRANSITION_NAME);
#else
		if (mrt_gate_observable() && gate_after) {
			zassert_true(mrt_image_equal(&after[i], &before[i]),
				     "%s: the programmed period did not survive the "
				     "transition: CTRL=0x%08x INTVAL=%u RUN=%d, want "
				     "0x%08x/%u/%d", c->dev->name, after[i].ctrl,
				     after[i].intval, (int)after[i].running, before[i].ctrl,
				     before[i].intval, (int)before[i].running);
		}

		/* And the block has to work, not just read back right. */
		zassert_true(mrt_advances(c),
			     "%s: the channel is not counting after the transition",
			     c->dev->name);
		zassert_true(mrt_wait_ticks(c, 2U, 1000U),
			     "%s: the callbacks the consumer programmed did not resume after "
			     "the transition (%u before, %u now)", c->dev->name,
			     ticks_before[i], mrt_ticks(c));
#endif

		(void)mrt_stop(c);
	}
}
