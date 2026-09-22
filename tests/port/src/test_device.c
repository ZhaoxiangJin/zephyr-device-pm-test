/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The device layer: the driver's PM callbacks, driven directly.
 *
 * SUSPEND and TURN_OFF are no-ops by design -- a pad configuration is state, not
 * activity, and it has to hold for as long as the block is powered -- so what this
 * layer really tests is TURN_ON: the one action with a hardware effect, and the only
 * thing standing between a pin-mux that lost power and a driver that re-applies its
 * pin state through a closed clock gate.
 */

#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "port.h"

static void port_device_cycle(const struct port_case *p)
{
	bool observable = port_gate_observable(p);
	bool closed_by_hand = false;
	bool open_after_turn_on = false;
	int turn_on_rc;

	/* No runtime PM in this layer, so pm_device_driver_init() resumed it. */
	zassert_pm_state(p->dev, PM_DEVICE_STATE_ACTIVE);

	zassert_ok(pm_device_action_run(p->dev, PM_DEVICE_ACTION_SUSPEND),
		   "%s: SUSPEND rejected", p->dev->name);
	if (observable) {
		zassert_true(port_gate_is_open(p), "%s: gate closed by SUSPEND", p->dev->name);
	}

	zassert_ok(pm_device_action_run(p->dev, PM_DEVICE_ACTION_TURN_OFF),
		   "%s: TURN_OFF rejected", p->dev->name);
	if (observable) {
		zassert_true(port_gate_is_open(p), "%s: gate closed by TURN_OFF", p->dev->name);
	}

	/*
	 * Nothing in this build can take power away, so close the gate by hand to
	 * stand in for the reset state the block comes back in, and let TURN_ON find
	 * it that way. Nothing is printed and nothing is asserted inside the window:
	 * the console's own pin-mux may be this very instance. The PCR contents are
	 * held by the block rather than by the gate, so the pads keep their function
	 * while it is closed -- and if they did not, a garbled character here would
	 * itself be the finding.
	 */
	if (observable) {
		port_gate_close(p);
		closed_by_hand = !port_gate_is_open(p);
		turn_on_rc = pm_device_action_run(p->dev, PM_DEVICE_ACTION_TURN_ON);
		open_after_turn_on = port_gate_is_open(p);
		if (!open_after_turn_on) {
			/* Leave the board usable for the rest of the run, whatever the
			 * driver did, before any of this is judged.
			 */
			port_gate_open(p);
		}

		zassert_true(closed_by_hand, "%s: gate still reads open after being gated by hand",
			     p->dev->name);
		zassert_true(open_after_turn_on, "%s (PORT%u): TURN_ON did not re-open the clock "
			     "gate", p->dev->name, p->index);
	} else {
		turn_on_rc = pm_device_action_run(p->dev, PM_DEVICE_ACTION_TURN_ON);
	}

	zassert_ok(turn_on_rc, "%s: TURN_ON rejected", p->dev->name);

	zassert_ok(pm_device_action_run(p->dev, PM_DEVICE_ACTION_RESUME), "%s: RESUME rejected",
		   p->dev->name);
	zassert_pm_state(p->dev, PM_DEVICE_STATE_ACTIVE);
}

ZTEST(port_pm, test_device_turn_on_reopens_the_gate)
{
	for (size_t i = 0U; i < PORT_CASE_COUNT; i++) {
		port_device_cycle(&port_cases[i]);
	}
}
