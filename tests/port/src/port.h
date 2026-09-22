/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief What every layer of the PORT case needs from the hardware.
 *
 * The driver's PM callback has exactly one hardware effect: TURN_ON asks
 * clock_control_on() to re-open the instance's pin-mux clock gate. Nothing else in
 * the callback touches a register, so the gate bit is the only hardware truth this
 * case can assert on.
 *
 * It is read out of the clock controller, never out of the PORT block: a PCR only
 * answers while the block is clocked, so reading one is exactly what must not be
 * done to decide whether the clock is on. No file in this case touches a PCR.
 */

#ifndef PORT_PM_H_
#define PORT_PM_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

/** One enabled nxp,port-pinmux instance, and which PORTn it is. */
struct port_case {
	const struct device *dev;
	uint8_t index;
};

#define PORT_COUNT_ONE(node_id) 1 +

/** How many pin-mux instances this board enables; usable as an array bound. */
#define PORT_CASE_COUNT (DT_FOREACH_STATUS_OKAY(nxp_port_pinmux, PORT_COUNT_ONE) 0)

/** Every enabled instance, in devicetree order. */
extern const struct port_case port_cases[PORT_CASE_COUNT];

/**
 * @brief Does this instance have a clock gate the test can read back?
 *
 * Not every one does. On MCXN the HAL defines kCLOCK_Port0..Port4 only, even on
 * parts whose FSL_FEATURE_SOC_PORT_COUNT is 6: portf sits outside the address
 * window the other five share and has no gate of its own. On MCXA there is a
 * constant for every instance the part actually has. Where there is no constant
 * there is nothing to check, and a layer says so rather than quietly passing.
 */
bool port_gate_observable(const struct port_case *p);

/** @brief Is this instance's pin-mux clock gate open? */
bool port_gate_is_open(const struct port_case *p);

/**
 * @brief Open the gate behind the driver's back.
 *
 * Used only to leave the board usable after a layer found the gate closed.
 */
void port_gate_open(const struct port_case *p);

/**
 * @brief Close the gate behind the driver's back.
 *
 * Nothing in a build without a real power loss can close a gate, so this stands in
 * for the reset state the block comes back in. clock_control_off() would not do:
 * the syscon clock driver has no PORT case in its .off handler at all and just
 * returns 0.
 */
void port_gate_close(const struct port_case *p);

/** @brief Note an instance's gate in the log without judging it. */
void port_report_gate(const struct port_case *p, const char *when);

#endif /* PORT_PM_H_ */
