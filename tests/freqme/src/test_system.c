/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The system layer: the per-device power state constraint.
 *
 * The FREQME node lists the states that stop it in zephyr,disabling-power-states
 * (see constraints.overlay), and the driver holds a policy constraint against
 * exactly those states for as long as the block is measuring, in either mode. The
 * two modes end differently, and that is what the two tests are about.
 *
 * A MEASURE ends on its own: the hardware disarms after one window and the API
 * needs no clock_monitor_stop() on that path. So the interrupt that completes the
 * cycle has to hand the constraint back, or one measurement blocks those states
 * for the rest of the run.
 *
 * WINDOW monitoring has no end of its own, so the constraint follows the
 * monitoring: stop() and a runtime-PM suspend hand it back, and a resume that
 * re-arms the monitor takes it again.
 */

#include <zephyr/drivers/clock_monitor.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/pm/policy.h>
#include <zephyr/pm/state.h>
#include <zephyr/ztest.h>

#include <pm_test/lowpower.h>

#include "freqme.h"

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

ZTEST(freqme_pm, test_system_constraint_ends_with_a_measurement)
{
	const struct device *dev = freqme_dev();
	unsigned int declared;
	unsigned int before;
	unsigned int after_configure;
	unsigned int while_measuring;
	unsigned int mismatch;
	unsigned int after_completion;
	int configure_rc;
	int start_rc;
	int wait_rc;

	declared = declared_states(dev);
	if (declared == 0U) {
		/* No state is declared to stop this instance, so there is no constraint
		 * to observe. Said rather than passed quietly.
		 */
		TC_PRINT("%s: declares no disabling power state, nothing asserted\n",
			 dev->name);
		ztest_test_skip();
	}

	pm_test_console_quiesce();
	before = locked_states();
	zassert_ok(pm_device_runtime_get(dev), "%s: runtime_get rejected", dev->name);

	/* Configured but not measuring: nothing to protect yet. A long window, so the
	 * measurement is still running when the lock is sampled.
	 */
	configure_rc = freqme_configure_measure(FREQME_LONG_WINDOW_NS);
	pm_test_console_quiesce();
	after_configure = locked_states();

	start_rc = freqme_measure_start();
	while_measuring = locked_states();
	mismatch = constraint_mismatch(dev);

	/* No stop(): the completion is what has to release the constraint. */
	wait_rc = freqme_measure_wait(1000U);
	pm_test_console_quiesce();
	after_completion = locked_states();

	(void)pm_device_runtime_put(dev);

	zassert_equal(before, 0U, "%s: %u power state(s) were already locked", dev->name,
		      before);
	zassert_ok(configure_rc, "%s: configure() failed (%d)", dev->name, configure_rc);
	zassert_equal(after_configure, 0U,
		      "%s: %u power state(s) locked by a configured but idle block", dev->name,
		      after_configure);
	zassert_ok(start_rc, "%s: start() failed (%d)", dev->name, start_rc);
	zassert_equal(while_measuring, declared, "%s: %u state(s) locked while measuring, %u "
		      "declared", dev->name, while_measuring, declared);
	zassert_equal(mismatch, 0U, "%s: %u power state(s) whose lock does not match the node",
		      dev->name, mismatch);
	zassert_ok(wait_rc, "%s: the measurement did not complete (%d)", dev->name, wait_rc);
	zassert_equal(after_completion, 0U,
		      "%s: %u power state(s) still locked after the measurement completed",
		      dev->name, after_completion);
}

ZTEST(freqme_pm, test_system_constraint_follows_the_window)
{
	const struct device *dev = freqme_dev();
	unsigned int declared;
	unsigned int while_monitoring;
	unsigned int after_stop;
	unsigned int while_suspended;
	unsigned int after_resume;
	unsigned int at_end;
	int start_rc;
	int restart_rc;

	declared = declared_states(dev);
	if (declared == 0U) {
		TC_PRINT("%s: declares no disabling power state, nothing asserted\n",
			 dev->name);
		ztest_test_skip();
	}

	pm_test_console_quiesce();
	zassert_ok(pm_device_runtime_get(dev), "%s: runtime_get rejected", dev->name);

	start_rc = freqme_start_window(false);
	while_monitoring = locked_states();
	(void)clock_monitor_stop(dev);
	pm_test_console_quiesce();
	after_stop = locked_states();

	/* The last reference goes away, so the driver suspends and monitoring stops --
	 * and with it the reason for the lock.
	 */
	restart_rc = clock_monitor_start(dev);
	(void)pm_device_runtime_put(dev);
	pm_test_console_quiesce();
	while_suspended = locked_states();

	/*
	 * A resume re-arms the monitor the consumer started, so it has to take the
	 * protection back: without it the monitor comes back running while the policy
	 * is free to enter a state that stops it.
	 */
	zassert_ok(pm_device_runtime_get(dev), "%s: second runtime_get rejected", dev->name);
	after_resume = locked_states();

	(void)clock_monitor_stop(dev);
	(void)pm_device_runtime_put(dev);
	pm_test_console_quiesce();
	at_end = locked_states();

	zassert_ok(start_rc, "%s: could not start WINDOW (%d)", dev->name, start_rc);
	zassert_equal(while_monitoring, declared,
		      "%s: %u state(s) locked while monitoring, %u declared", dev->name,
		      while_monitoring, declared);
	zassert_equal(after_stop, 0U, "%s: %u power state(s) still locked after stop()",
		      dev->name, after_stop);
	zassert_ok(restart_rc, "%s: could not restart WINDOW (%d)", dev->name, restart_rc);
	zassert_equal(while_suspended, 0U, "%s: %u power state(s) still locked while suspended",
		      dev->name, while_suspended);
	zassert_equal(after_resume, declared,
		      "%s: %u state(s) locked after the monitor came back, %u declared",
		      dev->name, after_resume, declared);
	zassert_equal(at_end, 0U, "%s: %u power state(s) leaked past the end of the test",
		      dev->name, at_end);
}
