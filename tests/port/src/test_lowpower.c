/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The sysmanaged and dpd layers: the device sweep across one transition.
 *
 * One file for two layers, because the sequence is identical and only the state
 * being entered differs -- and that difference is the whole point. Deep Sleep only
 * gates clocks, so the pin-mux comes back untouched. Deep Power Down takes the CORE
 * domain down, which is the only thing that actually closes a pin-mux clock gate and
 * therefore the only thing that reaches the driver's TURN_ON.
 *
 * On a board whose SoC has a portf the dpd layer is expected to fail: the driver asks
 * clock_control_on() to re-open the gate and clock_control_mcux_syscon.c has no
 * MCUX_PORT5_CLK case in either family branch -- port5-clock-gate-never-reopened in
 * docs/findings.md.
 */

#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>

#include <pm_test/lowpower.h>
#include <pm_test/state.h>

#include "port.h"

static bool gate_open_after[PORT_CASE_COUNT];
static bool observable[PORT_CASE_COUNT];

/*
 * Sample every gate and re-open by hand any the transition left closed. Runs before
 * the console is touched, and prints nothing: the console's own pin-mux may be one of
 * the instances that came back gated, and reviving the LPUART re-applies its pin
 * state, which writes a PCR through that gate.
 */
static void sample_and_reopen_gates(void)
{
	for (size_t i = 0U; i < PORT_CASE_COUNT; i++) {
		const struct port_case *p = &port_cases[i];

		observable[i] = port_gate_observable(p);
		if (!observable[i]) {
			continue;
		}

		gate_open_after[i] = port_gate_is_open(p);
		if (!gate_open_after[i]) {
			port_gate_open(p);
		}
	}
}

ZTEST(port_pm, test_sweep_across_transition)
{
	/* No runtime PM in these layers, so the sweep is the only thing that will
	 * suspend the pin-mux instances.
	 */
	for (size_t i = 0U; i < PORT_CASE_COUNT; i++) {
		zassert_pm_state(port_cases[i].dev, PM_DEVICE_STATE_ACTIVE);
		port_report_gate(&port_cases[i], "before");
	}

	pm_test_enter_transition();
	sample_and_reopen_gates();

	/*
	 * Zero flags: the pin-mux instances are deliberately not re-initialised, which
	 * is the whole point of this case. Everything the console needs from them has
	 * to have been put back by the domain's TURN_ON and the loop above -- and
	 * surviving console output from here on is itself part of the evidence.
	 */
	pm_test_console_resume(0U);
	TC_PRINT("resumed from %s\n", PM_TEST_TRANSITION_NAME);

	for (size_t i = 0U; i < PORT_CASE_COUNT; i++) {
		const struct port_case *p = &port_cases[i];

		zassert_pm_state(p->dev, PM_DEVICE_STATE_ACTIVE);

		if (!observable[i]) {
			TC_PRINT("%s (PORT%u) has no observable gate on this SoC\n", p->dev->name,
				 p->index);
			continue;
		}

		zassert_true(gate_open_after[i],
			     "%s (PORT%u): clock gate closed after the transition", p->dev->name,
			     p->index);
	}
}
