/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The system layer: the per-channel power state constraint.
 *
 * Each channel node lists the states that stop it counting in
 * zephyr,disabling-power-states (see constraints.overlay), and the driver holds a
 * policy constraint against exactly those states for as long as that channel is
 * actually counting. The window is not one API call -- a counter has no completion
 * event -- so the constraint follows the counter and the device state together: a
 * suspend that stops the channel hands the lock back, and a resume that finds
 * STAT[RUN] still set takes it again. Without that, one counter_start() would block
 * those states for the rest of the run even with no consumer left.
 *
 * The list is on the channel and not on the module because the channel is the device
 * a consumer holds and the device the driver passes to
 * pm_policy_device_power_lock_get(). That placement is what the third test is about:
 * the policy count is per state and refcounted, so two running channels hold a state
 * twice and stopping one must leave the other's protection standing.
 */

#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/pm/policy.h>
#include <zephyr/pm/state.h>
#include <zephyr/ztest.h>

#include <pm_test/lowpower.h>

#include "mrt.h"

/* How many power states are locked right now, by anyone. */
static unsigned int locked_states(void)
{
	unsigned int locked = 0;

	for (enum pm_state state = PM_STATE_ACTIVE; state < PM_STATE_COUNT; state++) {
		locked += pm_policy_state_lock_is_active(state, 0) ? 1U : 0U;
	}

	return locked;
}

/*
 * How many power states this node declares as disabling. States that are listed but
 * disabled in the devicetree are dropped from the generated table, so this can
 * legitimately be smaller than the list in constraints.overlay. A zero means the
 * overlay never reached the node, which is why the tests below guard on it.
 */
static unsigned int declared_states(const struct device *dev)
{
	unsigned int declared = 0;

	for (enum pm_state state = PM_STATE_ACTIVE; state < PM_STATE_COUNT; state++) {
		declared += pm_policy_device_is_disabling_state(dev, state, 0) ? 1U : 0U;
	}

	return declared;
}

/* Locked states that the node does not declare, or declared states left unlocked. */
static unsigned int constraint_mismatch(const struct device *dev)
{
	unsigned int mismatch = 0;

	for (enum pm_state state = PM_STATE_ACTIVE; state < PM_STATE_COUNT; state++) {
		bool locked = pm_policy_state_lock_is_active(state, 0);
		bool disabling = pm_policy_device_is_disabling_state(dev, state, 0);

		mismatch += (locked != disabling) ? 1U : 0U;
	}

	return mismatch;
}

ZTEST(mrt_pm, test_system_constraint_covers_the_declared_states)
{
	MRT_FOREACH_CASE(c) {
		unsigned int before;
		unsigned int declared = declared_states(c->dev);

		if (declared == 0U) {
			TC_PRINT("%s: declares no disabling power states, nothing asserted\n",
				 c->dev->name);
			continue;
		}

		before = locked_states();
		zassert_equal(before, 0U,
			      "%s: %u state(s) already locked before the channel started",
			      c->dev->name, before);

		zassert_ok(mrt_start(c), "%s: could not start the channel", c->dev->name);
		zassert_true(mrt_wait_ticks(c, 2U, 500U), "%s: no callbacks", c->dev->name);

		/* The claim: exactly the declared states, and no others. */
		zassert_equal(constraint_mismatch(c->dev), 0U,
			      "%s: %u locked state(s) do not match the %u the node declares",
			      c->dev->name, locked_states(), declared);

		(void)mrt_stop(c);
		zassert_equal(locked_states(), 0U,
			      "%s: %u state(s) still locked after the channel stopped",
			      c->dev->name, locked_states());
	}
}

ZTEST(mrt_pm, test_system_constraint_follows_the_channel)
{
	MRT_FOREACH_CASE(c) {
		unsigned int declared = declared_states(c->dev);

		if (declared == 0U) {
			TC_PRINT("%s: declares no disabling power states, nothing asserted\n",
				 c->dev->name);
			continue;
		}

		zassert_ok(mrt_start(c), "%s: could not start the channel", c->dev->name);
		zassert_equal(locked_states(), declared, "%s: %u state(s) locked, want %u",
			      c->dev->name, locked_states(), declared);

		/*
		 * A suspended channel is not counting, so holding off the states it
		 * declares would keep the system out of them for a block with nothing to
		 * lose there. The lock goes back on suspend.
		 */
		zassert_ok(pm_device_action_run(c->dev, PM_DEVICE_ACTION_SUSPEND),
			   "%s: SUSPEND action rejected", c->dev->name);
		zassert_equal(locked_states(), 0U,
			      "%s: %u state(s) still locked while the channel is suspended",
			      c->dev->name, locked_states());

		/* And comes back on resume, because STAT[RUN] is still set. */
		zassert_true(mrt_running(c),
			     "%s: the channel stopped counting across a suspend that only "
			     "released the lock", c->dev->name);
		zassert_ok(pm_device_action_run(c->dev, PM_DEVICE_ACTION_RESUME),
			   "%s: RESUME action rejected", c->dev->name);
		zassert_equal(locked_states(), declared,
			      "%s: %u state(s) locked after RESUME, want %u", c->dev->name,
			      locked_states(), declared);

		(void)mrt_stop(c);
		zassert_equal(locked_states(), 0U, "%s: %u state(s) left locked",
			      c->dev->name, locked_states());
	}
}

ZTEST(mrt_pm, test_system_one_channel_does_not_release_another)
{
	const struct mrt_case *first = &mrt_cases[0];
	const struct mrt_case *second;
	unsigned int declared;

	if (MRT_CASE_COUNT < 2) {
		TC_PRINT("only one channel is enabled, so no sibling to protect\n");
		ztest_test_skip();
	}

	second = &mrt_cases[1];
	declared = declared_states(first->dev);

	if (declared == 0U) {
		TC_PRINT("the channels declare no disabling power states, nothing asserted\n");
		ztest_test_skip();
	}

	zassert_ok(mrt_start(first), "%s: could not start", first->dev->name);
	zassert_ok(mrt_start(second), "%s: could not start", second->dev->name);
	zassert_equal(locked_states(), declared, "%u state(s) locked with two channels "
		      "running, want %u", locked_states(), declared);

	/*
	 * The claim of this test. The policy count is per state and refcounted, so two
	 * running channels hold each declared state twice. Stopping one takes the count
	 * to one, not to zero, and the state stays blocked for the channel still
	 * running.
	 */
	(void)mrt_stop(second);
	zassert_equal(locked_states(), declared,
		      "stopping %s released the states %s still needs: %u locked, want %u",
		      second->dev->name, first->dev->name, locked_states(), declared);
	zassert_true(mrt_running(first), "%s: stopped when %s did", first->dev->name,
		     second->dev->name);

	/* Only the last one out releases them. */
	(void)mrt_stop(first);
	zassert_equal(locked_states(), 0U,
		      "%u state(s) still locked after both channels stopped", locked_states());
}
