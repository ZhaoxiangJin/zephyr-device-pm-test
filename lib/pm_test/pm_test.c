/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <pm_test/layer.h>

#if defined(CONFIG_PM)
#include <pm_test/lowpower.h>
#endif

void *pm_test_setup(void)
{
	TC_PRINT("layer: %s\n", PM_TEST_LAYER);
	return NULL;
}

#if defined(CONFIG_PM)

void pm_test_console_quiesce(void)
{
	/*
	 * One character takes 1 ms even at 9600 baud, and printk() has already
	 * waited for every earlier one, so 5 ms is several times the worst case.
	 * Interrupts stay on: the UART's transmission-complete interrupt is what
	 * drops the lock, so masking it here would defeat the wait.
	 */
	k_busy_wait(5000);
}

#if defined(CONFIG_PM_TEST_LAYER_DPD)

#define CONSOLE_NODE   DT_CHOSEN(zephyr_console)
#define CONSOLE_PARENT DT_PARENT(CONSOLE_NODE)

/*
 * Deep Power Down resets CORE-domain peripherals. The device under test is
 * restored through TURN_ON. This helper separately reinitializes the reporting
 * console: PORT pin-mux devices when requested, the LP_FLEXCOMM parent when
 * present, then LPUART.
 */
#define REINIT_DEVICE(dev)                                                                         \
	do {                                                                                       \
		(dev)->state->initialized = false;                                                 \
		(void)device_init(dev);                                                            \
	} while (0)

#define REINIT_PINMUX(node_id) REINIT_DEVICE(DEVICE_DT_GET(node_id));

void pm_test_console_resume(uint32_t flags)
{
	if ((flags & PM_TEST_CONSOLE_REINIT_PINMUX) != 0U) {
		DT_FOREACH_STATUS_OKAY(nxp_port_pinmux, REINIT_PINMUX)
	}

#if DT_NODE_HAS_COMPAT(CONSOLE_PARENT, nxp_lp_flexcomm)
	REINIT_DEVICE(DEVICE_DT_GET(CONSOLE_PARENT));
#endif
	REINIT_DEVICE(DEVICE_DT_GET(CONSOLE_NODE));
}

#else /* the transition leaves the console powered */

void pm_test_console_resume(uint32_t flags)
{
	ARG_UNUSED(flags);
}

#endif /* CONFIG_PM_TEST_LAYER_DPD */

#if defined(PM_TEST_TRANSITION_STATE)

void pm_test_enter_transition(void)
{
	TC_PRINT("forcing one %s transition\n", PM_TEST_TRANSITION_NAME);
	k_busy_wait(2000); /* let the console finish shifting that line out */

	/*
	 * Forced rather than left to the policy, so that the transition is a
	 * single deterministic event at a known point in the sequence and the
	 * board stays awake -- and attachable -- for the rest of the run.
	 */
	pm_state_force(0U, &(struct pm_state_info){PM_TEST_TRANSITION_STATE, 0U, 0U});
	k_sleep(K_SECONDS(2));
}

#endif /* PM_TEST_TRANSITION_STATE */

#endif /* CONFIG_PM */
