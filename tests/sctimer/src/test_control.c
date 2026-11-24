/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The control: this case really does drive the SCTimer.
 *
 * Compiled into every layer. If this fails, no verdict from the layer under test
 * means anything.
 *
 * Three claims: that a configured channel leaves a register image distinguishable
 * from a reset block, that the counter counts, and that the output toggles. Every
 * later layer tells "restored" from "came back from reset" by comparing against that
 * image, and tells a restored block from one that reads back right but is not driving
 * anything by watching the output.
 */

#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#include "sctimer.h"

ZTEST(sctimer_pm, test_control_drives_the_output)
{
	SCTIMER_FOREACH_CASE(c) {
		struct sctimer_image configured;
		bool held = false;
		bool counting;
		bool toggling;
		int put_rc = 0;
		int rc;

		/*
		 * Under runtime PM the device boots suspended and this driver takes no
		 * reference of its own, so the control has to ask for it. That an
		 * unwrapped call is refused is test_runtime.c's claim, not a premise
		 * of the control.
		 */
		if (pm_device_runtime_is_enabled(c->dev)) {
			zassert_ok(pm_device_runtime_get(c->dev),
				   "%s: could not resume the device for the control",
				   c->dev->name);
			held = true;
		}

		rc = sctimer_configure(c);
		sctimer_image_read(c, &configured);
		counting = sctimer_advances(c);
		toggling = sctimer_output_toggles(c);
		sctimer_report_hw(c, "control");

		if (held) {
			/* Before the assertions: a failure must not leak the reference. */
			put_rc = pm_device_runtime_put(c->dev);
		}

		zassert_ok(put_rc, "%s: control runtime_put failed", c->dev->name);
		zassert_ok(rc, "%s: pwm_set_cycles() failed (%d)", c->dev->name, rc);

		/*
		 * The prescaler this driver writes from devicetree is 0 on every board
		 * here, which is also what a reset leaves -- so the match reloads and
		 * the output events are the only register state in this block that a
		 * restore can be seen to put back.
		 */
		zassert_false(sctimer_image_is_clear(&configured),
			      "%s: a configured channel left the registers at reset",
			      c->dev->name);
		zassert_true(counting, "%s: the counter is not counting", c->dev->name);
		zassert_true(toggling, "%s: the output is not toggling", c->dev->name);
	}
}

ZTEST(sctimer_pm, test_control_duty_and_polarity)
{
	static const struct {
		uint32_t pulse;
		pwm_flags_t flags;
	} waveforms[] = {
		{SCTIMER_PWM_PULSE, PWM_POLARITY_NORMAL},
		{0U, PWM_POLARITY_NORMAL},
		{SCTIMER_PWM_PERIOD, PWM_POLARITY_NORMAL},
		{SCTIMER_PWM_PULSE, PWM_POLARITY_INVERTED},
		{0U, PWM_POLARITY_INVERTED},
		{SCTIMER_PWM_PERIOD, PWM_POLARITY_INVERTED},
	};

	SCTIMER_FOREACH_CASE(c) {
		bool held = pm_device_runtime_is_enabled(c->dev);
		bool observed[ARRAY_SIZE(waveforms)] = {false};
		int set_rc = 0;
		int restore_rc;
		int put_rc = 0;

		if (held) {
			zassert_ok(pm_device_runtime_get(c->dev), "%s: runtime_get failed",
				   c->dev->name);
		}
		for (size_t index = 0U; index < ARRAY_SIZE(waveforms); index++) {
			bool inverted = waveforms[index].flags == PWM_POLARITY_INVERTED;

			set_rc = pwm_set_cycles(c->dev, SCTIMER_PWM_CHANNEL, SCTIMER_PWM_PERIOD,
						waveforms[index].pulse, waveforms[index].flags);
			if (set_rc != 0) {
				break;
			}
			if (waveforms[index].pulse == 0U) {
				observed[index] = sctimer_output_stays(c, inverted);
			} else if (waveforms[index].pulse == SCTIMER_PWM_PERIOD) {
				observed[index] = sctimer_output_stays(c, !inverted);
			} else {
				observed[index] = sctimer_output_toggles(c);
			}
		}
		restore_rc = sctimer_configure(c);
		if (held) {
			put_rc = pm_device_runtime_put(c->dev);
		}

		zassert_ok(set_rc, "%s: waveform programming failed (%d)", c->dev->name, set_rc);
		zassert_ok(restore_rc, "%s: restoring normal PWM failed", c->dev->name);
		zassert_ok(put_rc, "%s: runtime_put failed", c->dev->name);
		for (size_t index = 0U; index < ARRAY_SIZE(waveforms); index++) {
			zassert_true(observed[index], "%s: pulse %u flags %u has wrong output",
				     c->dev->name, waveforms[index].pulse, waveforms[index].flags);
		}
	}
}
