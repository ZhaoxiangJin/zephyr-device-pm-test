/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The device layer: the driver's PM callbacks, driven directly.
 *
 * The MRT splits its callbacks across two devices, so this layer has three tests
 * rather than two.
 *
 * The module owns the clock, so the module's SUSPEND and RESUME are what open and
 * close the one gate the four channels share. The block stays powered across them,
 * so a channel's register image has to come back unchanged.
 *
 * A channel owns only its policy constraint. Its SUSPEND and RESUME touch no
 * register, because the only power-relevant register is the shared gate and gating it
 * while a sibling counts would stop the sibling. What is asserted about them is the
 * state the core records and that the hardware is left alone.
 *
 * The module's TURN_OFF and TURN_ON own the register block. Nothing in this layer
 * really cut power to it, so one channel's CTRL is cleared by hand to give the
 * restore something to put back. What a real power cycle does is asked in
 * src/test_lowpower.c under the dpd layer.
 *
 * Note which action is expected to bring the block up. TURN_ON rebuilds the register
 * block, borrowing the clock and handing it back before it returns, so a module that
 * boots SUSPENDED under runtime PM boots configured but genuinely gated. RESUME
 * re-opens the gate. The third test asserts that split as far as it is observable: an
 * MRT register answers only while the block is clocked, so the image cannot be read
 * between TURN_ON and RESUME, and what is checked there is that TURN_ON left the gate
 * closed.
 */

#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "mrt.h"

ZTEST(mrt_pm, test_device_module_suspend_resume)
{
	const struct device *module = mrt_cases[0].module;
	struct mrt_image before[MRT_CASE_COUNT];
	struct mrt_image after;

	if (!mrt_gate_observable()) {
		TC_PRINT("clock gate not observable, nothing asserted\n");
		ztest_test_skip();
	}

	/* No runtime PM in this layer, so pm_device_driver_init() resumed everything. */
	zassert_pm_state(module, PM_DEVICE_STATE_ACTIVE);
	zassert_true(mrt_gate_is_open(), "gate closed while the module reports ACTIVE");

	MRT_FOREACH_CASE(c) {
		zassert_ok(mrt_start(c), "%s: could not start the channel", c->dev->name);
		mrt_image_read(c, &before[c - &mrt_cases[0]]);
		mrt_report_hw(c, "before-suspend");
	}

	zassert_ok(pm_device_action_run(module, PM_DEVICE_ACTION_SUSPEND),
		   "%s: SUSPEND action rejected", module->name);
	zassert_pm_state(module, PM_DEVICE_STATE_SUSPENDED);

	/*
	 * The claim of this test: the block is no longer clocked. Nothing is read out
	 * of it here -- an unclocked block does not answer, and reading it to find out
	 * whether it is clocked is the one thing that cannot be done.
	 */
	zassert_false(mrt_gate_is_open(), "clock gate still open after the module SUSPEND");

	MRT_FOREACH_CASE(c) {
		uint32_t stalled = mrt_ticks(c);

		k_msleep(MRT_PERIOD_US / 1000U * 3U);
		zassert_equal(mrt_ticks(c), stalled,
			      "%s: callbacks kept arriving while the module was suspended",
			      c->dev->name);
	}

	zassert_ok(pm_device_action_run(module, PM_DEVICE_ACTION_RESUME),
		   "%s: RESUME action rejected", module->name);
	zassert_pm_state(module, PM_DEVICE_STATE_ACTIVE);
	zassert_true(mrt_gate_is_open(), "gate still closed after the module RESUME");

	MRT_FOREACH_CASE(c) {
		mrt_image_read(c, &after);
		mrt_report_hw(c, "after-resume");

		/* The block kept its power, so what comes back is what was there. */
		zassert_true(mrt_image_equal(&after, &before[c - &mrt_cases[0]]),
			     "%s: RESUME left CTRL=0x%08x INTVAL=%u RUN=%d, want "
			     "0x%08x/%u/%d", c->dev->name, after.ctrl, after.intval,
			     (int)after.running, before[c - &mrt_cases[0]].ctrl,
			     before[c - &mrt_cases[0]].intval,
			     (int)before[c - &mrt_cases[0]].running);
		zassert_true(mrt_advances(c), "%s: the channel is not counting after RESUME",
			     c->dev->name);
		zassert_true(mrt_wait_ticks(c, 2U, 500U),
			     "%s: callbacks did not resume after RESUME", c->dev->name);
		(void)mrt_stop(c);
	}
}

ZTEST(mrt_pm, test_device_channel_suspend_touches_no_register)
{
	MRT_FOREACH_CASE(c) {
		struct mrt_image before;
		struct mrt_image after;

		if (!mrt_gate_observable()) {
			TC_PRINT("%s: clock gate not observable, nothing asserted\n",
				 c->dev->name);
			continue;
		}

		zassert_pm_state(c->dev, PM_DEVICE_STATE_ACTIVE);
		zassert_ok(mrt_start(c), "%s: could not start the channel", c->dev->name);
		mrt_image_read(c, &before);

		zassert_ok(pm_device_action_run(c->dev, PM_DEVICE_ACTION_SUSPEND),
			   "%s: SUSPEND action rejected", c->dev->name);
		zassert_pm_state(c->dev, PM_DEVICE_STATE_SUSPENDED);

		/*
		 * A suspended channel is still clocked, because the gate belongs to the
		 * module and its siblings may be running. So the hardware is untouched
		 * and still counting, and the only thing that changed is the state the
		 * core records and the policy constraint the channel released.
		 */
		zassert_true(mrt_gate_is_open(),
			     "%s: a channel SUSPEND closed the module's gate", c->dev->name);
		mrt_image_read(c, &after);
		zassert_true(mrt_image_equal(&after, &before),
			     "%s: a channel SUSPEND changed CTRL=0x%08x INTVAL=%u RUN=%d",
			     c->dev->name, after.ctrl, after.intval, (int)after.running);

		zassert_ok(pm_device_action_run(c->dev, PM_DEVICE_ACTION_RESUME),
			   "%s: RESUME action rejected", c->dev->name);
		zassert_pm_state(c->dev, PM_DEVICE_STATE_ACTIVE);
		mrt_report_hw(c, "after-channel-resume");

		(void)mrt_stop(c);
	}
}

ZTEST(mrt_pm, test_device_turn_on_rebuilds_the_block)
{
	const struct device *module = mrt_cases[0].module;
	struct mrt_image clobbered;
	struct mrt_image after;
	int err;

	if (!mrt_gate_observable()) {
		TC_PRINT("clock gate not observable, nothing asserted\n");
		ztest_test_skip();
	}

	MRT_FOREACH_CASE(c) {
		(void)mrt_stop(c);
	}

	zassert_ok(pm_device_action_run(module, PM_DEVICE_ACTION_SUSPEND),
		   "%s: SUSPEND action rejected", module->name);
	zassert_ok(pm_device_action_run(module, PM_DEVICE_ACTION_TURN_OFF),
		   "%s: TURN_OFF action rejected", module->name);
	zassert_pm_state(module, PM_DEVICE_STATE_OFF);

	/* Nothing cut power, so stand in for it: clear the register TURN_ON rebuilds. */
	MRT_FOREACH_CASE(c) {
		mrt_clobber(c, &clobbered);
		zassert_equal(clobbered.ctrl, 0U, "%s: the clobber did not take, CTRL=0x%08x",
			      c->dev->name, clobbered.ctrl);
	}

	zassert_ok(pm_device_action_run(module, PM_DEVICE_ACTION_TURN_ON),
		   "%s: TURN_ON action rejected", module->name);
	zassert_pm_state(module, PM_DEVICE_STATE_SUSPENDED);

	/*
	 * TURN_ON borrowed the clock to rebuild the block and handed it back, so the
	 * module is configured and genuinely gated. That is the one thing observable
	 * between TURN_ON and RESUME: the registers cannot be read yet.
	 */
	zassert_false(mrt_gate_is_open(), "TURN_ON left the clock gate open");

	zassert_ok(pm_device_action_run(module, PM_DEVICE_ACTION_RESUME),
		   "%s: RESUME action rejected", module->name);
	zassert_true(mrt_gate_is_open(), "gate still closed after RESUME");

	MRT_FOREACH_CASE(c) {
		mrt_image_read(c, &after);
		mrt_report_hw(c, "after-turn-on");

		/* What TURN_ON owes: the channel interrupt enable it set at init. */
		zassert_not_equal(after.ctrl & MRT_CHANNEL_CTRL_INTEN_MASK, 0U,
				  "%s: TURN_ON left CTRL=0x%08x, interrupt enable clear",
				  c->dev->name, after.ctrl);

		/*
		 * What it deliberately does not owe: the period. INTVAL is also the
		 * start trigger, so restoring it would restart a run nobody asked for.
		 * The channel comes back idle and startable, which is the claim.
		 */
		zassert_equal(after.intval, 0U,
			      "%s: TURN_ON reprogrammed INTVAL to %u on an idle channel",
			      c->dev->name, after.intval);
		zassert_false(after.running, "%s: TURN_ON left the channel running",
			      c->dev->name);

		zassert_ok(mrt_start(c), "%s: the channel will not start after TURN_ON",
			   c->dev->name);
		zassert_true(mrt_wait_ticks(c, 2U, 500U),
			     "%s: no callbacks after TURN_ON rebuilt the block", c->dev->name);
		(void)mrt_stop(c);
	}

	/* A redundant action must be refused, not obeyed and not fatal. */
	err = pm_device_action_run(module, PM_DEVICE_ACTION_TURN_ON);
	zassert_equal(err, -EALREADY, "%s: second TURN_ON returned %d, want -EALREADY",
		      module->name, err);
}
