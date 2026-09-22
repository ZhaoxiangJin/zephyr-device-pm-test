/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The runtime layer: a pin-mux PM calls suspended is still usable.
 *
 * That is the interesting claim here, not the reference counting. A pin-mux whose
 * gate depended on a RESUME would be unusable for most of a boot, because every
 * driver that applies a pin state does so from its own init -- long before anything
 * takes a runtime reference, and in the layers below a TURN_ON that may never arrive
 * at all.
 */

#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#include <pm_test/state.h>

#include "port.h"

static void port_runtime_cycle(const struct port_case *p)
{
	enum pm_device_state state;

	/*
	 * Two boot states are legal. A node with no power domain lands in SUSPENDED:
	 * pm_device_driver_init() ran TURN_ON and then stopped short of RESUME because
	 * runtime PM is about to take over. A node that names a domain lands in OFF,
	 * because the domain device is itself runtime enabled -- it is suspended right
	 * after its own init, so by the time the pin-mux initialises at PRE_KERNEL_1
	 * pm_device_is_powered() is already false and TURN_ON is skipped. With
	 * power-domain-soc-state-change that TURN_ON never arrives later either; see
	 * pd-soc-state-change-no-turn-on-under-runtime-pm in docs/findings.md.
	 */
	zassert_ok(pm_device_state_get(p->dev, &state), "%s: state query failed", p->dev->name);
	zassert_true(state == PM_DEVICE_STATE_SUSPENDED || state == PM_DEVICE_STATE_OFF,
		     "%s: boots %s under runtime PM, want suspended or off", p->dev->name,
		     pm_device_state_str(state));

	if (port_gate_observable(p)) {
		zassert_true(port_gate_is_open(p), "%s (PORT%u): gate closed while PM reports %s",
			     p->dev->name, p->index, pm_device_state_str(state));
	}

	zassert_ok(pm_device_runtime_get(p->dev), "%s: runtime_get rejected", p->dev->name);
	zassert_pm_state(p->dev, PM_DEVICE_STATE_ACTIVE);

	zassert_ok(pm_device_runtime_put(p->dev), "%s: runtime_put rejected", p->dev->name);
	zassert_pm_state(p->dev, PM_DEVICE_STATE_SUSPENDED);

	/* And the gate outlives the reference, for the same reason it predated it. */
	if (port_gate_observable(p)) {
		zassert_true(port_gate_is_open(p), "%s (PORT%u): gate closed by runtime_put",
			     p->dev->name, p->index);
	}
	port_report_gate(p, "after-put");
}

ZTEST(port_pm, test_runtime_gate_outlives_the_reference)
{
	for (size_t i = 0U; i < PORT_CASE_COUNT; i++) {
		port_runtime_cycle(&port_cases[i]);
	}
}
