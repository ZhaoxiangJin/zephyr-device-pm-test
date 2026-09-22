/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The device layer: regulator_nxp_vref_pm_action() driven directly.
 *
 * Two claims, in two tests, and they have to run in this order: the first is about
 * the restore path the driver takes when no consumer has asked for a voltage, and
 * once the second has called regulator_set_voltage() the driver has latched
 * trim_set and that path cannot be reached again in the same run.
 *
 * Both tests stand in for a reset by clobbering registers by hand, because in this
 * layer nothing cut power to the block -- so SUSPEND and TURN_OFF are expected to
 * be no-ops, and only TURN_ON has work to do. What TURN_ON does *not* restore is
 * asked in src/test_lowpower.c, the layer where the block really did lose power.
 */

#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "vref.h"

/* Observations from one clobber/SUSPEND/TURN_OFF/TURN_ON/RESUME cycle. Collected
 * first and asserted afterwards, so that a failure cannot hand the next test a
 * suspended device and be reported twice.
 */
struct cycle {
	int suspend_rc;
	int turn_off_rc;
	int turn_on_rc;
	int resume_rc;
	bool bits_clobbered;
	bool bits_after_suspend;
	bool bits_after_turn_off;
	bool bits_after_turn_on;
	uint32_t trim_after_turn_on;
	int32_t volt_after_turn_on;
	int volt_rc;
};

static void run_cycle(struct cycle *c)
{
	c->suspend_rc = pm_device_action_run(VREF_DEV, PM_DEVICE_ACTION_SUSPEND);
	c->bits_after_suspend = vref_config_bits_present();

	c->turn_off_rc = pm_device_action_run(VREF_DEV, PM_DEVICE_ACTION_TURN_OFF);
	c->bits_after_turn_off = vref_config_bits_present();

	c->turn_on_rc = pm_device_action_run(VREF_DEV, PM_DEVICE_ACTION_TURN_ON);
	c->bits_after_turn_on = vref_config_bits_present();
	c->trim_after_turn_on = vref_trim();
	c->volt_rc = regulator_get_voltage(VREF_DEV, &c->volt_after_turn_on);
	vref_report_hw("after-turn-on");

	c->resume_rc = pm_device_action_run(VREF_DEV, PM_DEVICE_ACTION_RESUME);
}

static void assert_cycle(const struct cycle *c)
{
	zassert_ok(c->suspend_rc, "SUSPEND rejected (%d)", c->suspend_rc);
	zassert_ok(c->turn_off_rc, "TURN_OFF rejected (%d)", c->turn_off_rc);
	zassert_ok(c->turn_on_rc, "TURN_ON rejected (%d)", c->turn_on_rc);
	zassert_ok(c->resume_rc, "RESUME rejected (%d)", c->resume_rc);
	zassert_pm_state(VREF_DEV, PM_DEVICE_STATE_ACTIVE);

	/* Whether the output is on is owned by the consumers through the reference
	 * count, not by the SoC power state, so neither of these may touch the block.
	 */
	zassert_false(c->bits_after_suspend, "SUSPEND reconfigured the block");
	zassert_false(c->bits_after_turn_off, "TURN_OFF reconfigured the block");

	zassert_true(c->bits_after_turn_on, "TURN_ON did not restore the devicetree "
					    "configuration bits (CSR 0x%08x)", vref_csr());
}

ZTEST(vref_pm, test_device_turn_on_restores_an_untrimmed_block)
{
	uint32_t clobber_code;
	struct cycle c;

	/* No runtime PM in this layer, so pm_device_driver_init() ran TURN_ON and
	 * RESUME and the block is configured.
	 */
	zassert_pm_state(VREF_DEV, PM_DEVICE_STATE_ACTIVE);
	zassert_true(vref_config_bits_present(), "block is not configured at boot (CSR 0x%08x)",
		     vref_csr());

	vref_config_bits_clobber();
	clobber_code = vref_trim_clobber();
	c.bits_clobbered = !vref_config_bits_present() && (vref_trim() == clobber_code);
	vref_report_hw("clobbered");

	run_cycle(&c);

	/* Before anything else: a clobber that did not take would make every
	 * assertion below pass without the driver having restored a thing.
	 */
	zassert_true(c.bits_clobbered, "the clobber did not take, so this test proves nothing");
	assert_cycle(&c);

	if (vref_trim_is_factory) {
		/* The factory trim is loaded at reset and the driver has no copy of
		 * it, so leaving it alone is the right answer. This run has overwritten
		 * it for good; nothing here depends on absolute accuracy.
		 */
		zassert_equal(c.trim_after_turn_on, clobber_code,
			      "TURN_ON wrote the factory trim register");
	} else {
		zassert_equal(c.trim_after_turn_on, 0U,
			      "TURN_ON left the untrimmed output at 0x%x instead of the bottom "
			      "of the range", c.trim_after_turn_on);
	}
}

ZTEST(vref_pm, test_device_turn_on_restores_a_consumer_trim)
{
	int32_t chosen_uv = 0;
	int32_t read_uv = 0;
	uint32_t clobber_code;
	struct cycle c;
	int rc;

	zassert_ok(vref_pick_voltage(&chosen_uv), "no voltage can be picked out of the trim range");
	rc = regulator_set_voltage(VREF_DEV, chosen_uv, chosen_uv);
	zassert_ok(rc, "regulator_set_voltage(%d) failed (%d)", chosen_uv, rc);
	zassert_ok(regulator_get_voltage(VREF_DEV, &read_uv), "regulator_get_voltage() failed");
	zassert_equal(read_uv, chosen_uv, "voltage read back as %d, want %d", read_uv, chosen_uv);

	vref_config_bits_clobber();
	clobber_code = vref_trim_clobber();
	c.bits_clobbered = !vref_config_bits_present() && (vref_trim() == clobber_code);
	vref_report_hw("clobbered");

	run_cycle(&c);

	zassert_true(c.bits_clobbered, "the clobber did not take, so this test proves nothing");
	assert_cycle(&c);

	/*
	 * The claim the whole case exists for: that trim is the consumer's choice of
	 * output, and after a reset of the block nothing but the driver's own copy of
	 * it can put it back.
	 */
	zassert_ok(c.volt_rc, "regulator_get_voltage() failed after TURN_ON (%d)", c.volt_rc);
	zassert_equal(c.volt_after_turn_on, chosen_uv,
		      "TURN_ON restored %d uV, but the consumer asked for %d uV",
		      c.volt_after_turn_on, chosen_uv);
}
