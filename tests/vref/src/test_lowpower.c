/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The sysmanaged and dpd layers: the device sweep across one transition.
 *
 * One file for two layers, because the sequence is identical and only the state
 * being entered differs -- and that difference is what separates the two claims.
 * Deep Sleep leaves the VREF register block and its bandgap alone, which is the
 * point of a reference a converter can keep using in a low-power state, so the
 * sweep's SUSPEND and RESUME are all that happen and every assertion below holds
 * trivially. Deep Power Down resets the block, so the core domain hands the driver
 * TURN_OFF and TURN_ON and the restore has to be real.
 *
 * Which makes dpd the only layer that can ask what TURN_ON does *not* restore.
 * configure_hw() opens with regulator_nxp_vref_disable() and nothing re-applies
 * regulator-initial-mode, so a consumer still holding an enabled reference is left
 * with a dead bandgap in STANDBY mode and gets no error saying so:
 * vref-output-not-reenabled-after-dpd in docs/findings.md, expected to fail here
 * until it is settled. Whose job the re-enable is, is genuinely arguable -- the
 * README argues both sides.
 *
 * Everything is collected before anything is asserted, including the recovery, so
 * that one run yields the whole picture: a reference that comes back dead but takes
 * the output again on a disable/enable cycle is a gap in the automatic restore, and
 * one that does not is a block that came back broken. Those are very different
 * findings and an aborted test would not tell them apart.
 */

#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>

#include <pm_test/lowpower.h>
#include <pm_test/state.h>

#include "vref.h"

ZTEST(vref_pm, test_sweep_across_transition)
{
	int32_t before_uv = 0;
	int32_t after_uv = 0;
	bool configured_after;
	bool stable_after;
	bool mode_after;
	bool enabled_after;
	int voltage_rc;
	int disable_rc;
	int enable_rc;
	bool stable_recovered;

	/* No runtime PM in these layers, so the sweep is the only thing that will
	 * suspend the device.
	 */
	zassert_pm_state(VREF_DEV, PM_DEVICE_STATE_ACTIVE);

	/* Become the consumer this layer is about: a voltage of our choosing, which
	 * only the driver's own copy can restore, and a reference held across the
	 * transition. In the dpd layer both live in retained SRAM, which is what makes
	 * the restore possible at all.
	 */
	zassert_ok(vref_pick_voltage(&before_uv), "no voltage can be picked out of the trim range");
	zassert_ok(regulator_set_voltage(VREF_DEV, before_uv, before_uv),
		   "regulator_set_voltage(%d) failed", before_uv);
	zassert_ok(regulator_enable(VREF_DEV), "regulator_enable() failed");
	zassert_true(vref_output_stable(), "output not stable before the transition");
	vref_report_hw("before");

	pm_test_enter_transition();

	/* Sampled before the console is revived: reviving it takes time, and the
	 * question is what the block looked like the moment the sweep handed it back.
	 */
	configured_after = vref_config_bits_present();
	stable_after = vref_output_stable();
	mode_after = vref_mode_is_configured();
	enabled_after = regulator_is_enabled(VREF_DEV);
	voltage_rc = regulator_get_voltage(VREF_DEV, &after_uv);

	/* The recovery, whichever way the above went -- after the console is back,
	 * because regulator_enable() spins on CSR[VREFST] and a block that came back
	 * dead would hang here with nothing to show for it.
	 */
	pm_test_console_resume(PM_TEST_CONSOLE_REINIT_PINMUX);
	TC_PRINT("resumed from %s\n", PM_TEST_TRANSITION_NAME);
	vref_report_hw("after-resume");

	disable_rc = regulator_disable(VREF_DEV);
	enable_rc = regulator_enable(VREF_DEV);
	stable_recovered = vref_output_stable();
	vref_report_hw("recovered");

	/*
	 * pm_resume_devices() walks the devices it suspended in init order, so the
	 * core domain's TURN_ON has already run by the time the regulator's own RESUME
	 * does.
	 */
	zassert_pm_state(VREF_DEV, PM_DEVICE_STATE_ACTIVE);

	/* What the driver's TURN_ON promises. */
	zassert_true(configured_after,
		     "configuration bits not restored after the transition (CSR 0x%08x)",
		     vref_csr());
	zassert_ok(voltage_rc, "regulator_get_voltage() failed after the transition (%d)",
		   voltage_rc);
	zassert_equal(after_uv, before_uv, "the consumer's voltage did not survive: %d uV, want %d",
		      after_uv, before_uv);

	/* What it does not. */
	zassert_true(enabled_after, "the reference count no longer shows the consumer's reference");
	zassert_true(mode_after, "regulator-initial-mode not restored after the transition");
	zassert_true(stable_after, "output not stable after the transition for a held reference");

	/* Only reached when the restore had no gap, and then it says the block is
	 * healthy rather than merely untouched.
	 */
	zassert_ok(disable_rc, "regulator_disable() failed after the transition (%d)", disable_rc);
	zassert_ok(enable_rc, "regulator_enable() failed after the transition (%d)", enable_rc);
	zassert_true(stable_recovered, "output not stable again after a disable/enable cycle");
}
