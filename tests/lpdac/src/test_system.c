/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The system layer: the per-device power state constraint.
 *
 * The DAC node lists the states that stop the output in
 * zephyr,disabling-power-states (see constraints.overlay), and the driver holds a
 * policy constraint against exactly those states for as long as its output is on.
 * The window is not one API call -- a DAC output has no completion event -- it is
 * the span between the write that turns the output on and the suspend that turns
 * it off, which under runtime PM is the span the consumer's reference already
 * describes. So the consumer asks for the device and gets protection from the
 * states that would break it without naming any of them.
 *
 * Two tests: that the constraint matches what the node declares and appears only
 * once there is an output to protect, and that it follows the output across a
 * suspend and a resume rather than being taken once and kept.
 */

#include <zephyr/pm/device_runtime.h>
#include <zephyr/pm/policy.h>
#include <zephyr/pm/state.h>
#include <zephyr/ztest.h>

#include <pm_test/lowpower.h>

#include "lpdac.h"

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
 * How many power states the DAC node declares as disabling. States that are listed
 * but disabled in the devicetree are dropped from the generated table, so this can
 * legitimately be smaller than the list in constraints.overlay -- but a zero means
 * the overlay never reached the node, and every comparison below would then pass
 * while proving nothing.
 */
static unsigned int declared_states(void)
{
	unsigned int declared = 0;

	for (enum pm_state state = PM_STATE_ACTIVE; state < PM_STATE_COUNT; state++) {
		declared += pm_policy_device_is_disabling_state(LPDAC_DEV, state, 0) ? 1U : 0U;
	}

	return declared;
}

/* Locked states that the node does not declare, or declared states left unlocked. */
static unsigned int constraint_mismatch(void)
{
	unsigned int mismatch = 0;

	for (enum pm_state state = PM_STATE_ACTIVE; state < PM_STATE_COUNT; state++) {
		bool locked = pm_policy_state_lock_is_active(state, 0);
		bool disabling = pm_policy_device_is_disabling_state(LPDAC_DEV, state, 0);

		mismatch += (locked != disabling) ? 1U : 0U;
	}

	return mismatch;
}

ZTEST(lpdac_pm, test_system_constraint_covers_the_declared_states)
{
	unsigned int before;
	unsigned int after_setup;
	unsigned int declared;
	unsigned int mismatch;
	unsigned int after_put;
	int setup_rc;
	int write_rc;
	bool on;

	pm_test_console_quiesce();
	before = locked_states();

	zassert_ok(pm_device_runtime_get(LPDAC_DEV), "runtime_get rejected");

	/* Configured, but GCR[DACEN] still clear: there is no output to protect yet. */
	setup_rc = lpdac_setup();
	after_setup = locked_states();

	write_rc = lpdac_write(LPDAC_MIDSCALE);
	on = lpdac_output_on();
	declared = declared_states();
	mismatch = constraint_mismatch();

	/* Released before anything is asserted: a failure must not leave a lock behind,
	 * which would silently block those states for the rest of the run.
	 */
	(void)pm_device_runtime_put(LPDAC_DEV);

	pm_test_console_quiesce();
	after_put = locked_states();

	zassert_equal(before, 0U, "%u power state(s) were already locked before the test",
		      before);
	zassert_ok(setup_rc, "channel setup failed (%d)", setup_rc);
	zassert_ok(write_rc, "write failed (%d)", write_rc);
	zassert_true(on, "GCR[DACEN] clear while a runtime reference was held");

	zassert_equal(after_setup, 0U,
		      "%u power state(s) locked by a DAC that was configured but not driving "
		      "an output", after_setup);
	zassert_true(declared > 0U,
		     "no disabling power state reached the constraint table; is "
		     "constraints.overlay applied to this board's DAC node?");
	zassert_equal(mismatch, 0U,
		      "%u power state(s) whose lock does not match what the node declares",
		      mismatch);
	zassert_equal(after_put, 0U,
		      "%u power state(s) still locked after the last consumer reference was "
		      "dropped", after_put);
}

ZTEST(lpdac_pm, test_system_constraint_follows_the_output)
{
	unsigned int declared;
	unsigned int while_on;
	unsigned int while_suspended;
	unsigned int after_resume;
	unsigned int at_end;
	int setup_rc;
	int write_rc;
	bool on_after_resume;

	pm_test_console_quiesce();

	zassert_ok(pm_device_runtime_get(LPDAC_DEV), "runtime_get rejected");
	setup_rc = lpdac_setup();
	write_rc = lpdac_write(LPDAC_MIDSCALE);
	declared = declared_states();
	while_on = locked_states();

	/* The last reference goes away, so the driver suspends and the output stops. */
	(void)pm_device_runtime_put(LPDAC_DEV);
	pm_test_console_quiesce();
	while_suspended = locked_states();

	/*
	 * A resume restores the output the consumer last asked for, so it has to restore
	 * the protection too. A driver that only took the constraint in write_value()
	 * would come back driving a pin with nothing stopping the policy from entering a
	 * state that kills it.
	 */
	zassert_ok(pm_device_runtime_get(LPDAC_DEV), "second runtime_get rejected");
	on_after_resume = lpdac_output_on();
	after_resume = locked_states();

	(void)pm_device_runtime_put(LPDAC_DEV);
	pm_test_console_quiesce();
	at_end = locked_states();

	zassert_ok(setup_rc, "channel setup failed (%d)", setup_rc);
	zassert_ok(write_rc, "write failed (%d)", write_rc);
	zassert_true(declared > 0U,
		     "no disabling power state reached the constraint table; is "
		     "constraints.overlay applied to this board's DAC node?");

	zassert_equal(while_on, declared, "%u state(s) locked while the output was on, %u "
		      "declared", while_on, declared);
	zassert_equal(while_suspended, 0U,
		      "%u power state(s) still locked while the DAC was suspended",
		      while_suspended);
	zassert_true(on_after_resume, "GCR[DACEN] clear after the output was resumed");
	zassert_equal(after_resume, declared,
		      "%u state(s) locked after the output was resumed, %u declared",
		      after_resume, declared);
	zassert_equal(at_end, 0U, "%u power state(s) leaked past the end of the test",
		      at_end);
}
