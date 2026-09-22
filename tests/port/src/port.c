/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief PORT pin-mux plumbing shared by every layer of this case.
 */

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>
#include <zephyr/tc_util.h>

#include <zephyr/dt-bindings/clock/mcux_lpc_syscon_clock.h>
#include <fsl_clock.h>

#include "port.h"

#if !DT_HAS_COMPAT_STATUS_OKAY(nxp_port_pinmux)
#error "No nxp,port-pinmux node is enabled on this board"
#endif

#if !defined(CONFIG_PINCTRL_NXP_PORT)
#error "CONFIG_PINCTRL_NXP_PORT is not enabled, so there is no driver to test"
#endif

/*
 * Every PORT instance on MCXN and MCXA takes its clock from the syscon node with a
 * single MCUX_PORTn_CLK cell, so the instance number is the distance from
 * MCUX_PORT0_CLK. The Kinetis, MCXC, MCXE and MCXL parts wire the same driver to a
 * sim/pcc/root-clock controller instead and would need a different mapping.
 */
#define PORT_ASSERT_SYSCON_CLOCK(node_id)                                                          \
	BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CLOCKS_CTLR(node_id), nxp_lpc_syscon),                  \
		     "this case reads the PORT clock gate out of the MCX clock controller and "     \
		     "only fits SoCs whose pin-mux clocks come from nxp,lpc-syscon");

DT_FOREACH_STATUS_OKAY(nxp_port_pinmux, PORT_ASSERT_SYSCON_CLOCK)

#define PORT_ENTRY(node_id)                                                                        \
	{                                                                                          \
		.dev = DEVICE_DT_GET(node_id),                                                     \
		.index = (uint8_t)(DT_CLOCKS_CELL(node_id, name) - MCUX_PORT0_CLK),                \
	},

const struct port_case port_cases[PORT_CASE_COUNT] = {
	DT_FOREACH_STATUS_OKAY(nxp_port_pinmux, PORT_ENTRY)
};

static bool port_gate_of(uint8_t index, clock_ip_name_t *gate)
{
#if defined(CONFIG_SOC_FAMILY_MCXA)
	/* Same instances, and the same guard, as the syscon clock driver's own PORT
	 * cases in clock_control_mcux_syscon.c.
	 */
	switch (index) {
	case 0:
		*gate = kCLOCK_GatePORT0;
		break;
	case 1:
		*gate = kCLOCK_GatePORT1;
		break;
	case 2:
		*gate = kCLOCK_GatePORT2;
		break;
	case 3:
		*gate = kCLOCK_GatePORT3;
		break;
#if defined(FSL_FEATURE_SOC_PORT_COUNT) && (FSL_FEATURE_SOC_PORT_COUNT > 4)
	case 4:
		*gate = kCLOCK_GatePORT4;
		break;
#endif
#if defined(FSL_FEATURE_SOC_PORT_COUNT) && (FSL_FEATURE_SOC_PORT_COUNT > 5)
	case 5:
		*gate = kCLOCK_GatePORT5;
		break;
#endif
	default:
		return false;
	}

	return *gate != kCLOCK_GateNotAvail;
#else
	switch (index) {
	case 0:
		*gate = kCLOCK_Port0;
		break;
	case 1:
		*gate = kCLOCK_Port1;
		break;
	case 2:
		*gate = kCLOCK_Port2;
		break;
	case 3:
		*gate = kCLOCK_Port3;
		break;
	case 4:
		*gate = kCLOCK_Port4;
		break;
	default:
		return false;
	}

	return *gate != kCLOCK_None;
#endif
}

bool port_gate_observable(const struct port_case *p)
{
	clock_ip_name_t gate;

	return port_gate_of(p->index, &gate);
}

/*
 * CLOCK_EnableClock()/CLOCK_DisableClock() write through the write-only SET/CLR
 * aliases, so the readable register is the one at the head of each group:
 * SYSCON->AHBCLKCTRL0..3 on MCXN (contiguous from 0x200), MRCC0->MRCC_GLB_CCn on
 * MCXA (group stride 0x10, which is what the HAL's CLK_GATE_REG_OFFSET() encodes).
 */
bool port_gate_is_open(const struct port_case *p)
{
	clock_ip_name_t gate;

	if (!port_gate_of(p->index, &gate)) {
		return false;
	}

#if defined(CONFIG_SOC_FAMILY_MCXA)
	const volatile uint32_t *reg =
		(const volatile uint32_t *)((uintptr_t)&MRCC0->MRCC_GLB_CC0 +
					    CLK_GATE_REG_OFFSET(gate));

	return (*reg & BIT(CLK_GATE_BIT_SHIFT(gate))) != 0U;
#else
	const volatile uint32_t *reg = &(&SYSCON->AHBCLKCTRL0)[CLK_GATE_ABSTRACT_REG_OFFSET(gate)];

	return (*reg & BIT(CLK_GATE_ABSTRACT_BITS_SHIFT(gate))) != 0U;
#endif
}

void port_gate_open(const struct port_case *p)
{
	clock_ip_name_t gate;

	if (port_gate_of(p->index, &gate)) {
		CLOCK_EnableClock(gate);
	}
}

void port_gate_close(const struct port_case *p)
{
	clock_ip_name_t gate;

	if (port_gate_of(p->index, &gate)) {
		CLOCK_DisableClock(gate);
	}
}

void port_report_gate(const struct port_case *p, const char *when)
{
	if (!port_gate_observable(p)) {
		TC_PRINT("gate(%s, %s) not observable on this SoC\n", when, p->dev->name);
		return;
	}

	TC_PRINT("gate(%s, %s) PORT%u = %u\n", when, p->dev->name, p->index,
		 port_gate_is_open(p) ? 1U : 0U);
}
