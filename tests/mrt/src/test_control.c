/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The control: this case really does drive the MRT.
 *
 * Compiled into every layer. If this fails, no verdict from the layer under test
 * means anything.
 *
 * Two claims: that a started channel delivers its top callback repeatedly, and that a
 * running channel leaves a register image distinguishable from a reset block. Every
 * later layer tells "restored" from "came back from reset" by comparing against that
 * image.
 */

#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#include "mrt.h"

ZTEST(mrt_pm, test_control_counts_and_ticks)
{
	MRT_FOREACH_CASE(c) {
		struct mrt_image running;
		bool held = false;
		int start_rc;
		bool ticked;

		/*
		 * Under runtime PM the channel boots suspended and this driver takes no
		 * reference of its own, so the control has to ask for it. That an
		 * unwrapped call is refused is test_runtime.c's claim, not a premise of
		 * the control.
		 */
		if (pm_device_runtime_is_enabled(c->dev)) {
			zassert_ok(pm_device_runtime_get(c->dev),
				   "%s: could not resume the channel for the control",
				   c->dev->name);
			held = true;
		}

		start_rc = mrt_start(c);
		mrt_image_read(c, &running);
		ticked = mrt_wait_ticks(c, 3U, 500U);

		(void)mrt_stop(c);
		mrt_report_hw(c, "control");

		if (held) {
			/* Before the assertions: a failure must not leak the reference. */
			(void)pm_device_runtime_put(c->dev);
		}

		zassert_ok(start_rc, "%s: could not program and start the channel (%d)",
			   c->dev->name, start_rc);
		zassert_true(ticked, "%s: only %u top callback(s) arrived, wanted 3",
			     c->dev->name, mrt_ticks(c));

		/*
		 * CTRL[MODE] is 0 for repeat-interrupt mode, which is also the reset
		 * value, so the interrupt enable and the period are the only register
		 * state in this block that a restore can be seen to put back.
		 */
		zassert_not_equal(running.ctrl & MRT_CHANNEL_CTRL_INTEN_MASK, 0U,
				  "%s: CTRL[INTEN] is clear on a channel that is delivering "
				  "callbacks", c->dev->name);
		zassert_not_equal(running.intval, 0U, "%s: INTVAL is 0 on a running channel",
				  c->dev->name);
		zassert_true(running.running, "%s: STAT[RUN] is clear on a running channel",
			     c->dev->name);
	}
}
