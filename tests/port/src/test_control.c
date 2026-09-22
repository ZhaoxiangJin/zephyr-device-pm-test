/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The control: every pin-mux instance is clocked, whatever PM thinks.
 *
 * Compiled into every layer, and in this case it is more than a control.
 * pinctrl_mcux_init() opens the gate itself, before handing over to PM, and every
 * driver that applies a pin state from its own init writes a PCR through that gate.
 * So this has to hold in every layer -- including the ones where PM reports the
 * pin-mux SUSPENDED or OFF. If it ever stops holding, those drivers fault on an
 * unclocked block.
 */

#include <zephyr/ztest.h>

#include "port.h"

ZTEST(port_pm, test_control_gate_open_at_init)
{
	for (size_t i = 0U; i < PORT_CASE_COUNT; i++) {
		const struct port_case *p = &port_cases[i];

		port_report_gate(p, "init");

		if (!port_gate_observable(p)) {
			continue;
		}

		zassert_true(port_gate_is_open(p), "%s (PORT%u) clock gate closed at init",
			     p->dev->name, p->index);
	}
}
