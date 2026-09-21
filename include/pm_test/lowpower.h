/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The one forced low-power transition, and surviving it.
 *
 * Only the sysmanaged and dpd layers use this. Both force a single system power
 * state from the test thread rather than waiting for the policy to pick one, so
 * that the transition happens at a known point in the sequence.
 *
 * The transition is split from console revival because who revives the console is
 * case-specific: a case testing the pin-mux driver must not re-initialise the
 * pin-mux on the way back, since that is the thing under test.
 */

#ifndef PM_TEST_LOWPOWER_H_
#define PM_TEST_LOWPOWER_H_

#include <stdint.h>
#include <zephyr/pm/pm.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_PM_TEST_LAYER_DPD)
/** The system state this layer forces. */
#define PM_TEST_TRANSITION_STATE PM_STATE_SUSPEND_TO_RAM
/** How to name that state in the log. */
#define PM_TEST_TRANSITION_NAME  "Deep Power Down"
#elif defined(CONFIG_PM_TEST_LAYER_SYSMANAGED)
#define PM_TEST_TRANSITION_STATE PM_STATE_SUSPEND_TO_IDLE
#define PM_TEST_TRANSITION_NAME  "Deep Sleep"
#endif

/**
 * @brief Re-initialise the pin-mux devices as well as the console itself.
 *
 * Wanted by every case except the one whose subject is the pin-mux driver.
 */
#define PM_TEST_CONSOLE_REINIT_PINMUX BIT(0)

/**
 * @brief Force one transition through this layer's state and come back.
 *
 * Returns with the core running again but, after a transition that cut power to
 * the console's register block, with no working console: nothing printed between
 * here and pm_test_console_resume() will be seen. Put only the case's own
 * register sampling in that window.
 */
void pm_test_enter_transition(void);

/**
 * @brief Bring the console back after pm_test_enter_transition().
 *
 * A no-op in layers whose transition leaves the console powered.
 *
 * @param flags PM_TEST_CONSOLE_REINIT_PINMUX, or 0 to leave the pin-mux alone.
 */
void pm_test_console_resume(uint32_t flags);

/**
 * @brief Wait for the console to go idle before reading a PM policy lock.
 *
 * pm_policy_state_lock_is_active() answers for the state, not for one device, and
 * the console's own UART is one of the devices that takes that lock. Reading it
 * while a line is still shifting out reports the console's lock, not the lock the
 * test is asking about.
 */
void pm_test_console_quiesce(void);

#endif /* PM_TEST_LOWPOWER_H_ */
