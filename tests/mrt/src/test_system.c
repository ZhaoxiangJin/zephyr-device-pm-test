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
 * the policy count is per state and refcounted, so two running channels hold each
 * declared state twice and releasing one must leave it locked for the other.
 *
 * Two things every test here has to do. The console holds policy locks of its own
 * while it is draining, so pm_test_console_quiesce() comes before every
 * locked_states() read. And the channel's clock belongs to the module, which only
 * resumes when a channel claims it, so a reference is taken before the counter is
 * expected to run at all. Samples are taken first, references released next, and
 * only then asserted, so a failing assertion cannot leave a lock behind.
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
		unsigned int after_get;
		unsigned int while_counting;
		unsigned int after_stop;
		unsigned int mismatch;
		unsigned int declared = declared_states(c->dev);
		int start_rc;

		if (declared == 0U) {
			TC_PRINT("%s: declares no disabling power state, nothing asserted\n",
				 c->dev->name);
			continue;
		}

		pm_test_console_quiesce();
		before = locked_states();

		zassert_ok(pm_device_runtime_get(c->dev), "%s: runtime_get rejected",
			   c->dev->name);

		/* Resumed, but the channel is not counting yet: nothing to protect. */
		pm_test_console_quiesce();
		after_get = locked_states();

		start_rc = mrt_start(c);
		while_counting = locked_states();
		mismatch = constraint_mismatch(c->dev);

		/* Released before anything is asserted: a failure must not leave a lock
		 * behind, which would silently block those states for the rest of the run.
		 */
		(void)mrt_stop(c);
		(void)pm_device_runtime_put(c->dev);

		pm_test_console_quiesce();
		after_stop = locked_states();

		zassert_equal(before, 0U, "%s: %u power state(s) were already locked",
			      c->dev->name, before);
		zassert_ok(start_rc, "%s: could not program and start the channel (%d)",
			   c->dev->name, start_rc);
		zassert_equal(after_get, 0U,
			      "%s: %u power state(s) locked by a resumed but stopped channel",
			      c->dev->name, after_get);
		zassert_equal(while_counting, declared,
			      "%s: %u state(s) locked while counting, %u declared",
			      c->dev->name, while_counting, declared);
		zassert_equal(mismatch, 0U,
			      "%s: %u power state(s) whose lock does not match the node",
			      c->dev->name, mismatch);
		zassert_equal(after_stop, 0U, "%s: %u power state(s) still locked after stop",
			      c->dev->name, after_stop);
	}
}

ZTEST(mrt_pm, test_system_constraint_follows_the_channel)
{
	MRT_FOREACH_CASE(c) {
		unsigned int declared = declared_states(c->dev);
		unsigned int while_counting;
		unsigned int while_suspended;
		unsigned int after_resume;
		unsigned int at_end;
		int start_rc;
		bool counting_after_resume;

		if (declared == 0U) {
			TC_PRINT("%s: declares no disabling power state, nothing asserted\n",
				 c->dev->name);
			continue;
		}

		pm_test_console_quiesce();

		zassert_ok(pm_device_runtime_get(c->dev), "%s: runtime_get rejected",
			   c->dev->name);
		start_rc = mrt_start(c);
		while_counting = locked_states();

		/* The last reference goes away, so the driver suspends the channel -- and
		 * with it the reason for the lock.
		 */
		(void)pm_device_runtime_put(c->dev);
		pm_test_console_quiesce();
		while_suspended = locked_states();

		/*
		 * A resume finds STAT[RUN] still set, so it has to take the protection
		 * back: without it the channel comes back counting while the policy is
		 * free to enter a state that gates its clock.
		 */
		zassert_ok(pm_device_runtime_get(c->dev), "%s: second runtime_get rejected",
			   c->dev->name);
		counting_after_resume = mrt_advances(c);
		after_resume = locked_states();

		(void)mrt_stop(c);
		(void)pm_device_runtime_put(c->dev);
		pm_test_console_quiesce();
		at_end = locked_states();

		zassert_ok(start_rc, "%s: could not program and start the channel (%d)",
			   c->dev->name, start_rc);
		zassert_equal(while_counting, declared,
			      "%s: %u state(s) locked while counting, %u declared",
			      c->dev->name, while_counting, declared);
		zassert_equal(while_suspended, 0U,
			      "%s: %u power state(s) still locked while suspended",
			      c->dev->name, while_suspended);
		zassert_true(counting_after_resume,
			     "%s: the channel is not counting after resume", c->dev->name);
		zassert_equal(after_resume, declared,
			      "%s: %u state(s) locked after the channel came back, %u declared",
			      c->dev->name, after_resume, declared);
		zassert_equal(at_end, 0U,
			      "%s: %u power state(s) leaked past the end of the test",
			      c->dev->name, at_end);
	}
}

ZTEST(mrt_pm, test_system_one_channel_does_not_release_another)
{
	const struct mrt_case *first = &mrt_cases[0];
	const struct mrt_case *second;
	unsigned int declared;
	unsigned int both_running;
	unsigned int one_released;
	unsigned int at_end;
	bool first_still_running;
	int first_rc;
	int second_rc;

	if (MRT_CASE_COUNT < 2) {
		TC_PRINT("only one channel is enabled, so no sibling to protect\n");
		ztest_test_skip();
	}

	second = &mrt_cases[1];
	declared = declared_states(first->dev);

	if (declared == 0U) {
		TC_PRINT("the channels declare no disabling power state, nothing asserted\n");
		ztest_test_skip();
	}

	zassert_ok(pm_device_runtime_get(first->dev), "%s: runtime_get rejected",
		   first->dev->name);
	zassert_ok(pm_device_runtime_get(second->dev), "%s: runtime_get rejected",
		   second->dev->name);

	first_rc = mrt_start(first);
	second_rc = mrt_start(second);

	pm_test_console_quiesce();
	both_running = locked_states();

	/*
	 * The claim of this test. The policy count is per state and refcounted, so two
	 * running channels hold each declared state twice. Taking one away leaves the
	 * count at one, and the state stays blocked for the channel still counting.
	 */
	(void)mrt_stop(second);
	(void)pm_device_runtime_put(second->dev);

	first_still_running = mrt_running(first);
	pm_test_console_quiesce();
	one_released = locked_states();

	(void)mrt_stop(first);
	(void)pm_device_runtime_put(first->dev);
	pm_test_console_quiesce();
	at_end = locked_states();

	zassert_ok(first_rc, "%s: could not start (%d)", first->dev->name, first_rc);
	zassert_ok(second_rc, "%s: could not start (%d)", second->dev->name, second_rc);
	zassert_equal(both_running, declared,
		      "%u state(s) locked with two channels running, %u declared",
		      both_running, declared);
	zassert_true(first_still_running, "%s: stopped when %s did", first->dev->name,
		     second->dev->name);
	zassert_equal(one_released, declared,
		      "releasing %s released the states %s still needs: %u locked, want %u",
		      second->dev->name, first->dev->name, one_released, declared);
	zassert_equal(at_end, 0U, "%u state(s) still locked after both channels stopped",
		      at_end);
}
