/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 *
 * Device PM test harness for the NXP PORT pin-mux driver
 * (drivers/pinctrl/pinctrl_nxp_port.c).
 *
 * The same source adapts to the PM layer selected at build time:
 *
 *   (baseline)                    no CONFIG_PM_DEVICE      -> gate sanity only
 *   CONFIG_PM_DEVICE              manual SUSPEND/TURN_OFF/TURN_ON/RESUME via
 *                                 pm_device_action_run()
 *   CONFIG_PM_DEVICE_RUNTIME      get/put reference counting
 *   CONFIG_PM (+ constraints)     device power-state constraints on the PORT nodes
 *   CONFIG_PM_DEVICE_SYSTEM_MANAGED  the system device sweep, across one forced
 *                                    Deep Sleep transition
 *   + CONFIG_PM_S2RAM and &deeppowerdown enabled: the same, across Deep Power
 *     Down, which is the only state that takes the pin-mux block down and
 *     therefore the only one that reaches the driver's TURN_ON
 *
 * Every phase prints a line prefixed "PM-TEST:". The run ends with
 * "PM-TEST: RESULT PASS" or "PM-TEST: RESULT FAIL".
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <zephyr/dt-bindings/clock/mcux_lpc_syscon_clock.h>
#include <fsl_clock.h>

#if !DT_HAS_COMPAT_STATUS_OKAY(nxp_port_pinmux)
#error "No nxp,port-pinmux node is enabled on this board"
#endif

#if !defined(CONFIG_PINCTRL_NXP_PORT)
#error "CONFIG_PINCTRL_NXP_PORT is not enabled, so there is no driver to test"
#endif

/*
 * PM_DEVICE_SYSTEM_MANAGED is `default y if !PM_DEVICE_RUNTIME` inside
 * `if PM_DEVICE` and has no dependency on PM, so it is set in the plain
 * device-PM layer too, where there is no system PM and therefore no sweep.
 * Testing CONFIG_PM as well is what keeps the manual phase in the layer it
 * belongs to.
 */
#if defined(CONFIG_PM) && defined(CONFIG_PM_DEVICE_SYSTEM_MANAGED)
#define PORT_SYSTEM_MANAGED_SWEEP 1
#endif

/*
 * Every PORT instance on MCXN and MCXA takes its clock from the syscon node with
 * a single MCUX_PORTn_CLK cell, so the instance number is the distance from
 * MCUX_PORT0_CLK. The Kinetis, MCXC, MCXE and MCXL parts wire the same driver to
 * a sim/pcc/root-clock controller instead and would need a different mapping.
 */
#define PORT_ASSERT_SYSCON_CLOCK(node_id)                                            \
	BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CLOCKS_CTLR(node_id), nxp_lpc_syscon),    \
		     "this case reads the PORT clock gate out of the MCX clock "      \
		     "controller and only fits SoCs whose pin-mux clocks come from "  \
		     "nxp,lpc-syscon");

DT_FOREACH_STATUS_OKAY(nxp_port_pinmux, PORT_ASSERT_SYSCON_CLOCK)

struct port_case {
	const struct device *dev;
	uint8_t index;
};

#define PORT_ENTRY(node_id)                                                          \
	{                                                                            \
		.dev = DEVICE_DT_GET(node_id),                                       \
		.index = (uint8_t)(DT_CLOCKS_CELL(node_id, name) - MCUX_PORT0_CLK),  \
	},

static const struct port_case ports[] = {
	DT_FOREACH_STATUS_OKAY(nxp_port_pinmux, PORT_ENTRY)
};

static bool test_failed;

/* Per-port check messages have to name the port, so they are built at runtime. */
static char msg[96];

#define PM_TEST_CHECK(cond, msg)                                              \
	do {                                                                  \
		if (cond) {                                                   \
			printk("PM-TEST: CHECK PASS - %s\n", msg);            \
		} else {                                                      \
			printk("PM-TEST: CHECK FAIL - %s\n", msg);            \
			test_failed = true;                                   \
		}                                                             \
	} while (0)

/*
 * The driver's PM callback has exactly one hardware effect: TURN_ON calls
 * clock_control_on() on the PORT instance, which ends in CLOCK_EnableClock() on
 * the SoC's pin-mux clock gate. Nothing else in the callback touches a register.
 *
 * So the gate bit is the only hardware truth this case can assert on, and it is
 * read out of the clock controller rather than out of the PORT block: the PCR
 * registers only answer while the block is clocked, so touching them is exactly
 * what must not be done to decide whether the clock is on. This sample never
 * reads or writes a PCR.
 *
 * Not every instance has a gate that can be observed:
 *
 *   MCXN  the HAL defines kCLOCK_Port0..Port4 only, even on the parts whose
 *         FSL_FEATURE_SOC_PORT_COUNT is 6. portf sits at 0x42000, outside the
 *         address window the other five share, and has no gate of its own.
 *   MCXA  kCLOCK_GatePORTn exists for every instance the part has, bounded by
 *         FSL_FEATURE_SOC_PORT_COUNT.
 *
 * Where there is no gate constant there is nothing to check, and the phase says
 * so instead of quietly passing.
 */
static bool port_gate_of(uint8_t index, clock_ip_name_t *gate)
{
#if defined(CONFIG_SOC_FAMILY_MCXA)
	/* Same instances, and the same guard, as the syscon clock driver's own
	 * PORT cases in clock_control_mcux_syscon.c.
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

/*
 * Read the gate bit back. CLOCK_EnableClock()/CLOCK_DisableClock() write through
 * the write-only SET/CLR aliases, so the readable register is the one at the
 * head of each group: SYSCON->AHBCLKCTRL0..3 on MCXN (contiguous from 0x200),
 * MRCC0->MRCC_GLB_CCn on MCXA (group stride 0x10, which is what the HAL's
 * CLK_GATE_REG_OFFSET() encodes).
 */
static bool port_gate_is_open(clock_ip_name_t gate)
{
#if defined(CONFIG_SOC_FAMILY_MCXA)
	const volatile uint32_t *reg = (const volatile uint32_t *)
		((uintptr_t)&MRCC0->MRCC_GLB_CC0 + CLK_GATE_REG_OFFSET(gate));

	return (*reg & BIT(CLK_GATE_BIT_SHIFT(gate))) != 0U;
#else
	const volatile uint32_t *reg =
		&(&SYSCON->AHBCLKCTRL0)[CLK_GATE_ABSTRACT_REG_OFFSET(gate)];

	return (*reg & BIT(CLK_GATE_ABSTRACT_BITS_SHIFT(gate))) != 0U;
#endif
}

static void report_gate(const struct port_case *p, const char *when)
{
	clock_ip_name_t gate;

	if (!port_gate_of(p->index, &gate)) {
		printk("PM-TEST: gate(%s, %s) not observable on this SoC\n", when,
		       p->dev->name);
		return;
	}

	printk("PM-TEST: gate(%s, %s) PORT%u = %u\n", when, p->dev->name, p->index,
	       port_gate_is_open(gate) ? 1U : 0U);
}

#if defined(CONFIG_PM_DEVICE)
static const char *pm_state_str(enum pm_device_state st)
{
	switch (st) {
	case PM_DEVICE_STATE_ACTIVE:
		return "ACTIVE";
	case PM_DEVICE_STATE_SUSPENDED:
		return "SUSPENDED";
	case PM_DEVICE_STATE_SUSPENDING:
		return "SUSPENDING";
	case PM_DEVICE_STATE_OFF:
		return "OFF";
	default:
		return "?";
	}
}

static void report_state(const struct device *dev, const char *when)
{
	enum pm_device_state st;
	int err = pm_device_state_get(dev, &st);

	if (err < 0) {
		printk("PM-TEST: state(%s, %s) query err %d\n", when, dev->name, err);
		return;
	}
	printk("PM-TEST: state(%s, %s) = %s\n", when, dev->name, pm_state_str(st));
}

static bool state_is(const struct device *dev, enum pm_device_state expected)
{
	enum pm_device_state st;

	return (pm_device_state_get(dev, &st) == 0) && (st == expected);
}
#endif /* CONFIG_PM_DEVICE -- only the PM phases query state, and twister builds
	* with -Werror=unused-function.
	*/

/* ---- Phase: baseline ----------------------------------------------------- */
static void phase_baseline(void)
{
	printk("PM-TEST: phase baseline\n");

	for (size_t i = 0U; i < ARRAY_SIZE(ports); i++) {
		const struct port_case *p = &ports[i];
		clock_ip_name_t gate;

		snprintk(msg, sizeof(msg), "%s is ready", p->dev->name);
		PM_TEST_CHECK(device_is_ready(p->dev), msg);

		report_gate(p, "init");

		if (!port_gate_of(p->index, &gate)) {
			continue;
		}

		/* pinctrl_mcux_init() opens the gate itself, before handing over
		 * to pm_device_driver_init(). Every driver that applies a pin
		 * state from its own init writes through these registers, so
		 * this has to hold in every layer, whatever PM thinks the state
		 * of the pin-mux is.
		 */
		snprintk(msg, sizeof(msg), "%s (PORT%u) clock gate open at init",
			 p->dev->name, p->index);
		PM_TEST_CHECK(port_gate_is_open(gate), msg);
	}
}

/* ---- Phase: manual device PM --------------------------------------------- */
#if defined(CONFIG_PM_DEVICE) && !defined(CONFIG_PM_DEVICE_RUNTIME) && \
	!defined(PORT_SYSTEM_MANAGED_SWEEP)
static void port_device_cycle(const struct port_case *p)
{
	clock_ip_name_t gate;
	bool observable = port_gate_of(p->index, &gate);
	bool closed_by_hand = false;
	bool open_after_turn_on = false;
	int err;

	report_state(p->dev, "init");
	snprintk(msg, sizeof(msg), "%s starts ACTIVE", p->dev->name);
	PM_TEST_CHECK(state_is(p->dev, PM_DEVICE_STATE_ACTIVE), msg);

	/* SUSPEND and TURN_OFF are no-ops by design: the pad configuration is
	 * state, not activity, and it has to hold for as long as the block is
	 * powered. Both must leave the gate alone.
	 */
	err = pm_device_action_run(p->dev, PM_DEVICE_ACTION_SUSPEND);
	snprintk(msg, sizeof(msg), "%s SUSPEND accepted", p->dev->name);
	PM_TEST_CHECK(err == 0, msg);
	if (observable) {
		snprintk(msg, sizeof(msg), "%s gate still open after SUSPEND",
			 p->dev->name);
		PM_TEST_CHECK(port_gate_is_open(gate), msg);
	}

	err = pm_device_action_run(p->dev, PM_DEVICE_ACTION_TURN_OFF);
	snprintk(msg, sizeof(msg), "%s TURN_OFF accepted", p->dev->name);
	PM_TEST_CHECK(err == 0, msg);
	if (observable) {
		snprintk(msg, sizeof(msg), "%s gate still open after TURN_OFF",
			 p->dev->name);
		PM_TEST_CHECK(port_gate_is_open(gate), msg);
	}

	/*
	 * Only losing power closes the gate, and nothing in this build can do
	 * that, so close it by hand to stand in for the reset state the block
	 * comes back in. Note that clock_control_off() would not do: the syscon
	 * clock driver has no PORT case in its .off handler at all, it just
	 * returns 0. Hence the HAL call.
	 *
	 * Keep the window as short as possible and print nothing inside it: the
	 * console's own pin-mux may be this very instance. The PCR contents are
	 * held by the block, not by the gate, so the pads keep their function
	 * while it is closed -- and if they did not, a garbled character here
	 * would itself be the finding.
	 */
	if (observable) {
		CLOCK_DisableClock(gate);
		closed_by_hand = !port_gate_is_open(gate);
		err = pm_device_action_run(p->dev, PM_DEVICE_ACTION_TURN_ON);
		open_after_turn_on = port_gate_is_open(gate);
		if (!open_after_turn_on) {
			/* Leave the board in a usable state for the rest of the
			 * run whatever the driver did.
			 */
			CLOCK_EnableClock(gate);
		}

		snprintk(msg, sizeof(msg), "%s gate reads closed once gated by hand",
			 p->dev->name);
		PM_TEST_CHECK(closed_by_hand, msg);
		snprintk(msg, sizeof(msg), "%s TURN_ON re-opens the clock gate",
			 p->dev->name);
		PM_TEST_CHECK(open_after_turn_on, msg);
	} else {
		err = pm_device_action_run(p->dev, PM_DEVICE_ACTION_TURN_ON);
	}

	snprintk(msg, sizeof(msg), "%s TURN_ON accepted", p->dev->name);
	PM_TEST_CHECK(err == 0, msg);

	err = pm_device_action_run(p->dev, PM_DEVICE_ACTION_RESUME);
	snprintk(msg, sizeof(msg), "%s RESUME accepted", p->dev->name);
	PM_TEST_CHECK(err == 0, msg);
	snprintk(msg, sizeof(msg), "%s is ACTIVE again", p->dev->name);
	PM_TEST_CHECK(state_is(p->dev, PM_DEVICE_STATE_ACTIVE), msg);
	report_state(p->dev, "after-cycle");
}

static void phase_device_pm(void)
{
	printk("PM-TEST: phase device-pm\n");
	printk("PM-TEST: sequence per port: SUSPEND, TURN_OFF, gate closed by hand, "
	       "TURN_ON, RESUME\n");

	for (size_t i = 0U; i < ARRAY_SIZE(ports); i++) {
		port_device_cycle(&ports[i]);
	}
}
#endif /* CONFIG_PM_DEVICE && !CONFIG_PM_DEVICE_RUNTIME && !PORT_SYSTEM_MANAGED_SWEEP */

/* ---- Phase: runtime device PM -------------------------------------------- */
#if defined(CONFIG_PM_DEVICE_RUNTIME)
static void port_runtime_cycle(const struct port_case *p)
{
	clock_ip_name_t gate;
	bool observable = port_gate_of(p->index, &gate);
	int err;

	report_state(p->dev, "init");
	/* Two boot states are legal here. A node with no power domain lands in
	 * SUSPENDED: pm_device_driver_init() ran TURN_ON and then stopped short of
	 * RESUME because runtime PM is about to take over. A node that names a
	 * domain lands in OFF instead, because the domain device is itself runtime
	 * enabled -- it is suspended right after its own init, so by the time the
	 * pin-mux initialises at PRE_KERNEL_1 pm_device_is_powered() is already
	 * false and TURN_ON is skipped. See README.md: with
	 * power-domain-soc-state-change that TURN_ON never arrives later either,
	 * because that domain only forwards TURN_ON on the system state changes it
	 * lists.
	 */
	snprintk(msg, sizeof(msg), "%s starts SUSPENDED or OFF under runtime PM", p->dev->name);
	PM_TEST_CHECK(state_is(p->dev, PM_DEVICE_STATE_SUSPENDED) ||
		      state_is(p->dev, PM_DEVICE_STATE_OFF), msg);

	/* The interesting claim of this layer: a pin-mux that PM calls SUSPENDED
	 * or OFF is still fully usable, because init opened the gate directly
	 * instead of leaving it to a TURN_ON or RESUME that may never come. If this
	 * fails, every driver that applies its pin state from its own init faults on
	 * an unclocked block.
	 */
	if (observable) {
		snprintk(msg, sizeof(msg), "%s gate open while not ACTIVE at init",
			 p->dev->name);
		PM_TEST_CHECK(port_gate_is_open(gate), msg);
	}

	err = pm_device_runtime_get(p->dev);
	snprintk(msg, sizeof(msg), "%s runtime_get succeeds", p->dev->name);
	PM_TEST_CHECK(err == 0, msg);
	snprintk(msg, sizeof(msg), "%s is ACTIVE after runtime_get", p->dev->name);
	PM_TEST_CHECK(state_is(p->dev, PM_DEVICE_STATE_ACTIVE), msg);

	err = pm_device_runtime_put(p->dev);
	snprintk(msg, sizeof(msg), "%s runtime_put succeeds", p->dev->name);
	PM_TEST_CHECK(err == 0, msg);
	snprintk(msg, sizeof(msg), "%s is SUSPENDED after runtime_put", p->dev->name);
	PM_TEST_CHECK(state_is(p->dev, PM_DEVICE_STATE_SUSPENDED), msg);

	if (observable) {
		snprintk(msg, sizeof(msg), "%s gate still open after runtime_put",
			 p->dev->name);
		PM_TEST_CHECK(port_gate_is_open(gate), msg);
	}
	report_gate(p, "after-put");
}

static void phase_runtime_pm(void)
{
	printk("PM-TEST: phase runtime-pm\n");

	for (size_t i = 0U; i < ARRAY_SIZE(ports); i++) {
		port_runtime_cycle(&ports[i]);
	}
}
#endif /* CONFIG_PM_DEVICE_RUNTIME */

/* ---- Phase: system PM device constraints (CONFIG_PM_POLICY_DEVICE_CONSTRAINTS) */
#if defined(CONFIG_PM_POLICY_DEVICE_CONSTRAINTS)
/*
 * pm_policy_state_lock_is_active() answers for the state, not for one device: it
 * is true while any holder blocks the state. The console is a holder -- under
 * CONFIG_PM the LPUART driver locks Deep Sleep and Power Down for as long as a
 * character is still shifting out and drops them from the transmission-complete
 * interrupt -- so a sample taken straight after a printk() reports the console
 * and says nothing about the pin-mux. Busy-wait, with interrupts on, for long
 * enough that the last character has left and that interrupt has run.
 */
static void console_quiesce(void)
{
	k_busy_wait(5000);
}

static void phase_system_constraints(void)
{
	printk("PM-TEST: phase system-constraints\n");
	printk("PM-TEST: constraints enabled; the PORT nodes carry "
	       "zephyr,disabling-power-states\n");

	/* The pin-mux driver never calls pm_policy_device_power_lock_get(), and
	 * should not: a pad configuration is not an operation in flight, so
	 * there is no window to protect. Declaring the states the block does not
	 * survive is still meaningful -- it is what an application would use to
	 * decide whether it has to re-apply pin state -- but nothing may end up
	 * holding a lock on the pin-mux's behalf.
	 */
	console_quiesce();
	PM_TEST_CHECK(!pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						      PM_ALL_SUBSTATES),
		      "no constraint held on the pin-mux's behalf");

	for (size_t i = 0U; i < ARRAY_SIZE(ports); i++) {
		report_gate(&ports[i], "constraints");
	}
}
#endif /* CONFIG_PM_POLICY_DEVICE_CONSTRAINTS */

/* ---- Phase: the system-managed device sweep ------------------------------ */
#if defined(PORT_SYSTEM_MANAGED_SWEEP)

/* Deep Power Down is only reachable when the application enables the state. */
#if DT_NODE_HAS_STATUS(DT_NODELABEL(deeppowerdown), okay) && defined(CONFIG_PM_S2RAM)
#define TRANSITION_STATE PM_STATE_SUSPEND_TO_RAM
#define TRANSITION_NAME  "Deep Power Down"
#else
#define TRANSITION_STATE PM_STATE_SUSPEND_TO_IDLE
#define TRANSITION_NAME  "Deep Sleep"
#endif

#if TRANSITION_STATE == PM_STATE_SUSPEND_TO_RAM
#define CONSOLE_NODE   DT_CHOSEN(zephyr_console)
#define CONSOLE_PARENT DT_PARENT(CONSOLE_NODE)

/*
 * Deep Power Down resets every CORE-domain peripheral, the console included, and
 * the LPUART driver has no hook that puts it back. Re-initialise it by hand so
 * the test can report its result -- but only the LP_FLEXCOMM parent and the
 * LPUART itself.
 *
 * The pin-mux instances are deliberately *not* re-initialised here, which is the
 * whole point of this case: device_init() on the LPUART re-applies its pin state,
 * which writes a PCR, which faults unless something has already re-opened that
 * PORT instance's clock gate. Under this PR the core domain's TURN_ON is what
 * does it. Surviving console output from here on is therefore evidence in itself.
 */
#define REINIT_DEVICE(dev)                                                    \
	do {                                                                  \
		(dev)->state->initialized = false;                            \
		(void)device_init(dev);                                       \
	} while (0)

static void resume_console(void)
{
#if DT_NODE_HAS_COMPAT(CONSOLE_PARENT, nxp_lp_flexcomm)
	REINIT_DEVICE(DEVICE_DT_GET(CONSOLE_PARENT));
#endif
	REINIT_DEVICE(DEVICE_DT_GET(CONSOLE_NODE));
}
#else
static void resume_console(void)
{
}
#endif /* TRANSITION_STATE == PM_STATE_SUSPEND_TO_RAM */

static bool gate_open_after[ARRAY_SIZE(ports)];
static bool gate_observable[ARRAY_SIZE(ports)];

/*
 * Sample every gate and re-open by hand any the transition left closed. This
 * runs before the console is touched, and prints nothing: the console's own
 * pin-mux may be one of the instances that came back gated, and re-initialising
 * the LPUART writes a PCR through that gate.
 */
static void sample_and_reopen_gates(void)
{
	for (size_t i = 0U; i < ARRAY_SIZE(ports); i++) {
		clock_ip_name_t gate;

		gate_observable[i] = port_gate_of(ports[i].index, &gate);
		if (!gate_observable[i]) {
			continue;
		}

		gate_open_after[i] = port_gate_is_open(gate);
		if (!gate_open_after[i]) {
			CLOCK_EnableClock(gate);
		}
	}
}

static void phase_system_managed(void)
{
	printk("PM-TEST: phase system-managed (%s)\n", TRANSITION_NAME);

	/* No runtime PM in this build, so pm_device_driver_init() resumed every
	 * pin-mux and the sweep is the only thing that will suspend them.
	 */
	for (size_t i = 0U; i < ARRAY_SIZE(ports); i++) {
		report_state(ports[i].dev, "init");
		snprintk(msg, sizeof(msg), "%s starts ACTIVE (system-managed)",
			 ports[i].dev->name);
		PM_TEST_CHECK(state_is(ports[i].dev, PM_DEVICE_STATE_ACTIVE), msg);
		report_gate(&ports[i], "before");
	}

	/* Only the state under test may be entered, and only when this phase asks
	 * for it, so the transition is a single deterministic event and the board
	 * stays awake -- and debuggable -- for the rest of the run.
	 */
	printk("PM-TEST: forcing one %s transition\n", TRANSITION_NAME);
	k_busy_wait(2000); /* let the console finish shifting the line out */

	pm_state_force(0U, &(struct pm_state_info){TRANSITION_STATE, 0U, 0U});
	k_sleep(K_SECONDS(2));

	sample_and_reopen_gates();
	resume_console();
	printk("PM-TEST: resumed from %s\n", TRANSITION_NAME);

	for (size_t i = 0U; i < ARRAY_SIZE(ports); i++) {
		report_state(ports[i].dev, "after-resume");
		snprintk(msg, sizeof(msg), "%s is ACTIVE again after the transition",
			 ports[i].dev->name);
		PM_TEST_CHECK(state_is(ports[i].dev, PM_DEVICE_STATE_ACTIVE), msg);

		if (!gate_observable[i]) {
			printk("PM-TEST: %s (PORT%u) has no observable gate on this "
			       "SoC\n", ports[i].dev->name, ports[i].index);
			continue;
		}

		snprintk(msg, sizeof(msg), "%s (PORT%u) clock gate open after the "
			 "transition", ports[i].dev->name, ports[i].index);
		PM_TEST_CHECK(gate_open_after[i], msg);
	}
}
#endif /* PORT_SYSTEM_MANAGED_SWEEP */

int main(void)
{
	printk("PM-TEST: BEGIN port device-pm on %s\n", CONFIG_BOARD_TARGET);
	printk("PM-TEST: %u pin-mux instance(s) under test\n", (unsigned)ARRAY_SIZE(ports));

	for (size_t i = 0U; i < ARRAY_SIZE(ports); i++) {
		printk("PM-TEST: %s = PORT%u\n", ports[i].dev->name, ports[i].index);
	}

	phase_baseline();

#if defined(CONFIG_PM_DEVICE) && !defined(CONFIG_PM_DEVICE_RUNTIME) && \
	!defined(PORT_SYSTEM_MANAGED_SWEEP)
	phase_device_pm();
#endif
#if defined(CONFIG_PM_DEVICE_RUNTIME)
	phase_runtime_pm();
#endif
#if defined(CONFIG_PM_POLICY_DEVICE_CONSTRAINTS)
	phase_system_constraints();
#endif
#if defined(PORT_SYSTEM_MANAGED_SWEEP)
	phase_system_managed();
#endif

	printk("PM-TEST: RESULT %s\n", test_failed ? "FAIL" : "PASS");

	/* Do not return. Returning from main() lets the idle thread park the
	 * core in WFI, which powers down the DAP: SWD access is then lost and
	 * the next flash attempt fails ("Failed to power up DAP", or the ROM
	 * dropping into its ISP command loop). Busy-waiting keeps the core out
	 * of idle so the board stays programmable after a run, and with
	 * CONFIG_PM it also keeps the PM subsystem from entering a low-power
	 * state behind the test's back.
	 */
	while (true) {
		k_busy_wait(USEC_PER_MSEC * 100U);
	}

	return 0;
}
