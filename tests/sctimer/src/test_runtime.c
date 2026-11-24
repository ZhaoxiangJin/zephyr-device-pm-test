/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The runtime layer: the consumer owns the output, and says so with a
 *        reference.
 *
 * This driver takes no runtime PM reference of its own. A PWM output drives until the
 * consumer changes it, and there is no completion event a put() could hang on, so
 * wrapping each API call in its own get/put would stop the output the moment the call
 * returned. The reference is the consumer's -- and because it is, a call that arrives
 * while nobody holds one cannot reach the peripheral and is refused rather than
 * silently writing a register behind a closed clock gate.
 *
 * What this layer also catches is the boot state. pm_device_driver_init() runs TURN_ON
 * and then stops short of RESUME when runtime PM is about to own the device, so a
 * driver that leaves the block clocked in TURN_ON boots it running while the core
 * records it as SUSPENDED. This driver's TURN_ON configures the block, parks it and
 * gates it again before returning, and the first test below is the check.
 *
 * Boot observations are saved by suite setup, before any test takes a reference.
 */

#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "sctimer.h"

static struct {
	enum pm_device_state state;
	enum pm_device_state before;
	enum pm_device_state after;
	int state_rc;
	int before_rc;
	int after_rc;
	int usage;
	int configure_rc;
	bool runtime_enabled;
	bool gate_open;
	bool gate_after;
} boot[SCTIMER_CASE_COUNT];

static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static bool console_held;

int sctimer_runtime_snapshot_boot(void)
{
	int err;

	TC_PRINT("runtime: saving boot observations\n");
	SCTIMER_FOREACH_CASE(c) {
		size_t index = c - sctimer_cases;

		boot[index].state_rc = pm_device_state_get(c->dev, &boot[index].state);
		boot[index].runtime_enabled = pm_device_runtime_is_enabled(c->dev);
		boot[index].usage = pm_device_runtime_usage(c->dev);
		boot[index].gate_open = sctimer_gate_is_open(c);
	}

	/* The reporting console is a separate consumer. Save DUT boot state first. */
	TC_PRINT("runtime: acquiring reporting console\n");
	err = pm_device_runtime_get(console);
	if (err != 0) {
		TC_PRINT("runtime: console get failed (%d)\n", err);
		return err;
	}
	console_held = pm_device_runtime_is_enabled(console);
	TC_PRINT("runtime: reporting console ready\n");

	SCTIMER_FOREACH_CASE(c) {
		size_t index = c - sctimer_cases;

		if (!device_is_ready(c->dev)) {
			boot[index].configure_rc = -ENODEV;
			continue;
		}
		boot[index].before_rc = pm_device_state_get(c->dev, &boot[index].before);
		TC_PRINT("runtime: checking unreferenced request on %s\n", c->dev->name);
		boot[index].configure_rc = sctimer_configure(c);
		TC_PRINT("runtime: unreferenced request returned %d\n", boot[index].configure_rc);
		boot[index].after_rc = pm_device_state_get(c->dev, &boot[index].after);
		boot[index].gate_after = sctimer_gate_is_open(c);
	}

	return 0;
}

void sctimer_runtime_release_console(void)
{
	if (console_held) {
		int err = pm_device_runtime_put(console);

		console_held = false;
		zassert_ok(err, "reporting console release failed (%d)", err);
	}
}

ZTEST(sctimer_pm, test_runtime_boots_without_a_consumer)
{
	SCTIMER_FOREACH_CASE(c) {
		size_t index = c - sctimer_cases;

		zassert_true(boot[index].runtime_enabled,
			     "%s: runtime PM is not enabled on this device", c->dev->name);
		zassert_ok(boot[index].state_rc, "%s: reading boot state failed", c->dev->name);
		zassert_equal(boot[index].usage, 0, "%s: booted with a reference", c->dev->name);

		/*
		 * Two boot states are legal. Without a power domain the device lands in
		 * SUSPENDED; with one it lands in OFF, because the domain is
		 * runtime-suspended before the SCTimer initialises and TURN_ON was
		 * skipped -- pd-soc-state-change-no-turn-on-under-runtime-pm in
		 * docs/findings.md.
		 */
		zassert_true((boot[index].state == PM_DEVICE_STATE_SUSPENDED) ||
				     (boot[index].state == PM_DEVICE_STATE_OFF),
			     "%s booted %s, want SUSPENDED or OFF under runtime PM",
			     c->dev->name, pm_device_state_str(boot[index].state));
		zassert_equal(boot[index].configure_rc, -EBUSY,
			      "%s: unreferenced boot request was not refused", c->dev->name);
		zassert_ok(boot[index].after_rc, "%s: reading post-request state failed",
			   c->dev->name);
		zassert_ok(boot[index].before_rc, "%s: reading pre-request state failed",
			   c->dev->name);
		zassert_true(boot[index].before == PM_DEVICE_STATE_OFF ||
			     boot[index].before == PM_DEVICE_STATE_SUSPENDED,
			     "%s: console activation left the DUT active", c->dev->name);
		zassert_equal(boot[index].after, boot[index].before,
			      "%s: refused request changed PM state", c->dev->name);

		if (!sctimer_gate_observable(c)) {
			TC_PRINT("%s: clock gate not observable, nothing asserted\n",
				 c->dev->name);
			continue;
		}

		/* The claim. Nothing may be clocked before a consumer has asked for it. */
		zassert_false(boot[index].gate_open || boot[index].gate_after,
			      "%s: clock gate open before any consumer took a reference",
			      c->dev->name);
	}
}

ZTEST(sctimer_pm, test_runtime_api_refuses_a_suspended_device)
{
	SCTIMER_FOREACH_CASE(c) {
		struct sctimer_peek before;
		struct sctimer_peek after;
		struct sctimer_image resumed;
		bool toggling;
		int err;

		/* Establish SUSPENDED even if boot left the device OFF. */
		TC_PRINT("runtime: establishing suspended API precondition\n");
		zassert_ok(pm_device_runtime_get(c->dev));
		zassert_ok(sctimer_configure(c));
		zassert_ok(pm_device_runtime_put(c->dev));
		zassert_pm_state(c->dev, PM_DEVICE_STATE_SUSPENDED);

		/*
		 * What the registers hold before the refused call, so the refusal can be
		 * shown to have been a refusal and not a write that happened to fail.
		 */
		sctimer_peek_gated(c, &before);

		TC_PRINT("runtime: checking suspended request\n");
		err = pwm_set_cycles(c->dev, SCTIMER_PWM_CHANNEL, 2U * SCTIMER_PWM_PERIOD,
				     SCTIMER_PWM_PULSE, PWM_POLARITY_INVERTED);
		TC_PRINT("runtime: suspended request returned %d\n", err);

		sctimer_peek_gated(c, &after);

		zassert_equal(err, -EBUSY,
			      "%s: pwm_set_cycles() on a suspended device returned %d, want "
			      "-EBUSY", c->dev->name, err);
		zassert_pm_state(c->dev, PM_DEVICE_STATE_SUSPENDED);

		if (sctimer_gate_observable(c)) {
			zassert_false(sctimer_gate_is_open(c),
				      "%s: the refused call left the clock gate open",
				      c->dev->name);
		}

		zassert_true(sctimer_image_equal(&after.image, &before.image),
			     "%s: the refused call changed the registers", c->dev->name);
		zassert_false(after.running, "%s: the refused call started the counter",
			      c->dev->name);
		zassert_equal(pm_device_runtime_usage(c->dev), 0,
			      "%s: refused request leaked a reference", c->dev->name);

		zassert_ok(pm_device_runtime_get(c->dev));
		sctimer_image_read(c, &resumed);
		toggling = sctimer_output_toggles(c);

		zassert_ok(pm_device_runtime_put(c->dev));
		zassert_true(sctimer_image_equal(&resumed, &before.image),
			     "%s: rejected request replaced the saved output", c->dev->name);
		zassert_true(toggling, "%s: old waveform did not restart", c->dev->name);
	}
}

ZTEST(sctimer_pm, test_runtime_reference_counting)
{
	SCTIMER_FOREACH_CASE(c) {
		struct sctimer_image configured;
		struct sctimer_image after;

		zassert_ok(pm_device_runtime_get(c->dev), "%s: runtime_get rejected",
			   c->dev->name);
		zassert_pm_state(c->dev, PM_DEVICE_STATE_ACTIVE);
		zassert_equal(pm_device_runtime_usage(c->dev), 1, "%s: first reference",
			      c->dev->name);

		if (sctimer_gate_observable(c)) {
			zassert_true(sctimer_gate_is_open(c),
				     "%s: gate closed while PM reports ACTIVE", c->dev->name);
		}

		zassert_ok(sctimer_configure(c), "%s: pwm_set_cycles() failed under a held "
			   "reference", c->dev->name);
		sctimer_image_read(c, &configured);
		zassert_true(sctimer_output_toggles(c), "%s: the output is not toggling under a "
			     "held reference", c->dev->name);

		/* A second reference must not be a second resume, and releasing it must not
		 * stop an output somebody else is still holding.
		 */
		zassert_ok(pm_device_runtime_get(c->dev), "%s: nested runtime_get rejected",
			   c->dev->name);
		zassert_equal(pm_device_runtime_usage(c->dev), 2, "%s: nested reference",
			      c->dev->name);
		zassert_ok(pm_device_runtime_put(c->dev), "%s: first runtime_put rejected",
			   c->dev->name);
		zassert_pm_state(c->dev, PM_DEVICE_STATE_ACTIVE);
		zassert_equal(pm_device_runtime_usage(c->dev), 1, "%s: first put",
			      c->dev->name);
		zassert_true(sctimer_advances(c), "%s: counter stopped while a reference was held",
			     c->dev->name);

		zassert_ok(pm_device_runtime_put(c->dev), "%s: last runtime_put rejected",
			   c->dev->name);
		zassert_pm_state(c->dev, PM_DEVICE_STATE_SUSPENDED);
		zassert_equal(pm_device_runtime_usage(c->dev), 0, "%s: final put",
			      c->dev->name);
		sctimer_report_hw(c, "after-put");

		if (sctimer_gate_observable(c)) {
			zassert_false(sctimer_gate_is_open(c),
				      "%s: clock gate still open after the last runtime_put",
				      c->dev->name);
		}

		/*
		 * And the consumer gets its output back by taking the reference again,
		 * without reconfiguring: what it programmed is the driver's to remember
		 * across a suspend the consumer did not ask for.
		 */
		zassert_ok(pm_device_runtime_get(c->dev),
			   "%s: runtime_get after the put rejected", c->dev->name);
		sctimer_image_read(c, &after);
		sctimer_report_hw(c, "after-reget");

		zassert_true(sctimer_image_equal(&after, &configured),
			     "%s: the channel did not come back with the reference: "
			     "MATCHREL0=0x%08x MATCHREL1=0x%08x SET=0x%08x CLR=0x%08x, want "
			     "0x%08x/0x%08x/0x%08x/0x%08x", c->dev->name, after.matchrel_period,
			     after.matchrel_pulse, after.out_set, after.out_clr,
			     configured.matchrel_period, configured.matchrel_pulse,
			     configured.out_set, configured.out_clr);
		zassert_true(sctimer_output_toggles(c),
			     "%s: the output is not toggling after the counter came back",
			     c->dev->name);

		/* Left as it was found, so this test's output does not outlive it. */
		zassert_ok(pm_device_runtime_put(c->dev), "%s: final runtime_put rejected",
			   c->dev->name);
	}
}

ZTEST(sctimer_pm, test_runtime_repeated_release)
{
	SCTIMER_FOREACH_CASE(c) {
		for (unsigned int cycle = 0U; cycle < 3U; cycle++) {
			struct sctimer_peek parked;
			bool toggling;

			zassert_ok(pm_device_runtime_get(c->dev));
			zassert_ok(sctimer_configure(c));
			toggling = sctimer_output_toggles(c);
			zassert_ok(pm_device_runtime_put(c->dev));

			zassert_true(toggling, "%s: no output in cycle %u", c->dev->name, cycle);
			zassert_pm_state(c->dev, PM_DEVICE_STATE_SUSPENDED);
			zassert_equal(pm_device_runtime_usage(c->dev), 0,
				      "%s: reference leaked in cycle %u", c->dev->name, cycle);
			zassert_false(sctimer_gate_is_open(c),
				      "%s: clock leaked in cycle %u", c->dev->name, cycle);
			sctimer_peek_gated(c, &parked);
			zassert_false(parked.running, "%s: counter was not halted", c->dev->name);
			zassert_false(parked.output_high, "%s: output was not parked",
				      c->dev->name);
		}
	}
}
