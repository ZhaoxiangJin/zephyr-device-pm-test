/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 *
 * Device PM test harness for the NXP VREF regulator driver
 * (drivers/regulator/regulator_nxp_vref.c).
 *
 * The same source adapts to the PM layer selected at build time:
 *
 *   (baseline)                    no CONFIG_PM_DEVICE      -> configuration sanity
 *   CONFIG_PM_DEVICE              manual SUSPEND/TURN_OFF/TURN_ON/RESUME via
 *                                 pm_device_action_run()
 *   CONFIG_PM_DEVICE_RUNTIME      get/put reference counting
 *   CONFIG_PM (+ constraints)     device power-state constraints on the vref node
 *   CONFIG_PM_DEVICE_SYSTEM_MANAGED  the system device sweep, across one forced
 *                                    Deep Sleep transition
 *   + CONFIG_PM_S2RAM and &deeppowerdown enabled: the same, across Deep Power
 *     Down, which is the only state that resets the VREF block and therefore the
 *     only one that reaches the driver's TURN_ON for real
 *
 * Every phase prints a line prefixed "PM-TEST:". The run ends with
 * "PM-TEST: RESULT PASS" or "PM-TEST: RESULT FAIL".
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/dt-bindings/regulator/nxp_vref.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <fsl_device_registers.h>

#define VREF_NODE DT_ALIAS(test_vref)

#if !DT_NODE_HAS_STATUS(VREF_NODE, okay)
#error "No test-vref alias resolving to an enabled nxp,vref node on this board"
#endif

#if !DT_NODE_HAS_COMPAT(VREF_NODE, nxp_vref)
#error "test-vref must name an nxp,vref node; nxp,vrefv1 is a different driver"
#endif

#if !defined(CONFIG_REGULATOR_NXP_VREF)
#error "CONFIG_REGULATOR_NXP_VREF is not enabled, so there is no driver to test"
#endif

#define VREF_DEV DEVICE_DT_GET(VREF_NODE)

static VREF_Type *const base = (VREF_Type *)DT_REG_ADDR(VREF_NODE);

/*
 * PM_DEVICE_SYSTEM_MANAGED is `default y if !PM_DEVICE_RUNTIME` inside
 * `if PM_DEVICE` and has no dependency on PM, so it is set in the plain
 * device-PM layer too, where there is no system PM and therefore no sweep.
 * Testing CONFIG_PM as well is what keeps the manual phase in the layer it
 * belongs to.
 */
#if defined(CONFIG_PM) && defined(CONFIG_PM_DEVICE_SYSTEM_MANAGED)
#define VREF_SYSTEM_MANAGED_SWEEP 1
#endif

#if defined(CONFIG_PM_DEVICE) && !defined(CONFIG_PM_DEVICE_RUNTIME) && \
	!defined(VREF_SYSTEM_MANAGED_SWEEP)
#define VREF_DEVICE_PM_PHASE 1
#endif

/*
 * Same variant split as the driver: the parts whose output buffer is a fixed
 * nominal 1.2 V expose only a factory-loaded UTRIM[VREFTRIM] fine trim, and the
 * driver leaves that register alone when no consumer has asked for a voltage.
 * Everywhere else the trim is UTRIM[TRIM2V1] and the driver's untrimmed restore
 * path drives it to the bottom of the range. Both MCXN families take the second
 * branch: they do not define FSL_FEATURE_VREF_HAS_TRIM2V1 at all.
 */
#if defined(FSL_FEATURE_VREF_HAS_TRIM2V1) && (FSL_FEATURE_VREF_HAS_TRIM2V1 == 0)
#define VREF_TRIM_MASK       VREF_UTRIM_VREFTRIM_MASK
#define VREF_TRIM_SHIFT      VREF_UTRIM_VREFTRIM_SHIFT
#define VREF_TRIM_IS_FACTORY 1
#else
#define VREF_TRIM_MASK       VREF_UTRIM_TRIM2V1_MASK
#define VREF_TRIM_SHIFT      VREF_UTRIM_TRIM2V1_SHIFT
#define VREF_TRIM_IS_FACTORY 0
#endif

/*
 * The bits TURN_ON is expected to put back, taken from the same devicetree
 * properties the driver reads, so this expectation follows the board rather than
 * restating one board's configuration.
 */
#define VREF_DT_CSR_BITS                                                             \
	((DT_PROP(VREF_NODE, nxp_current_compensation_en) ? VREF_CSR_ICOMPEN_MASK    \
							  : 0U) |                    \
	 (DT_PROP(VREF_NODE, nxp_chop_oscillator_en) ? VREF_CSR_CHOPEN_MASK : 0U) |  \
	 (DT_PROP(VREF_NODE, nxp_internal_voltage_regulator_en) ? VREF_CSR_REGEN_MASK \
								: 0U))

/*
 * regulator-initial-mode is applied by regulator_common_init() at boot. When the
 * node does not carry one, nothing calls set_mode() and the block is left in the
 * state configure_hw() leaves it in, which get_mode() reads back as STANDBY.
 */
#define VREF_DT_MODE DT_PROP_OR(VREF_NODE, regulator_initial_mode, NXP_VREF_MODE_STANDBY)

/* An in-range trim code that is neither the bottom of the range nor the one the
 * voltage phase picks, so a restore can be told apart from "nothing happened".
 */
#define VREF_TRIM_CLOBBER 0x3U

static bool test_failed;

#define PM_TEST_CHECK(cond, msg)                                              \
	do {                                                                  \
		if (cond) {                                                   \
			printk("PM-TEST: CHECK PASS - %s\n", msg);            \
		} else {                                                      \
			printk("PM-TEST: CHECK FAIL - %s\n", msg);            \
			test_failed = true;                                   \
		}                                                             \
	} while (0)

/* ---- Register-level observation ------------------------------------------- */
/*
 * The driver's PM callback restores hardware state that is written once and then
 * only lost to a reset of the block, so what this case observes is the register
 * content itself: the three CSR configuration bits the devicetree asks for, the
 * UTRIM trim code, and the CSR mode and status bits. All of them live in the VREF
 * block, which is always clocked here -- the MCXN vref node carries no clocks
 * property, so the driver's clock branch is skipped and there is no gate to
 * worry about.
 */
static uint32_t vref_trim(void)
{
	return (base->UTRIM & VREF_TRIM_MASK) >> VREF_TRIM_SHIFT;
}

#if defined(VREF_DEVICE_PM_PHASE)
static void vref_trim_clobber(void)
{
	base->UTRIM = (base->UTRIM & ~VREF_TRIM_MASK) |
		      ((VREF_TRIM_CLOBBER << VREF_TRIM_SHIFT) & VREF_TRIM_MASK);
}
#endif /* VREF_DEVICE_PM_PHASE -- only that phase stands in for a reset, and
	* twister builds with -Werror=unused-function.
	*/

static bool vref_output_stable(void)
{
	return (base->CSR & VREF_CSR_VREFST_MASK) != 0U;
}

static const char *mode_str(regulator_mode_t mode)
{
	switch (mode) {
	case NXP_VREF_MODE_STANDBY:
		return "STANDBY";
	case NXP_VREF_MODE_LOW_POWER:
		return "LOW_POWER";
	case NXP_VREF_MODE_HIGH_POWER:
		return "HIGH_POWER";
	default:
		return "?";
	}
}

static bool mode_is(regulator_mode_t expected)
{
	regulator_mode_t mode;

	return (regulator_get_mode(VREF_DEV, &mode) == 0) && (mode == expected);
}

static void report_hw(const char *when)
{
	regulator_mode_t mode = NXP_VREF_MODE_STANDBY;
	int32_t volt_uv = 0;

	(void)regulator_get_mode(VREF_DEV, &mode);
	(void)regulator_get_voltage(VREF_DEV, &volt_uv);

	printk("PM-TEST: hw(%s) CSR = 0x%08x, trim = 0x%x, mode = %s, %d uV, "
	       "VREFST = %u, refcount enabled = %u\n",
	       when, base->CSR, vref_trim(), mode_str(mode), volt_uv,
	       vref_output_stable() ? 1U : 0U,
	       regulator_is_enabled(VREF_DEV) ? 1U : 0U);
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

static void report_state(const char *when)
{
	enum pm_device_state st;
	int err = pm_device_state_get(VREF_DEV, &st);

	if (err < 0) {
		printk("PM-TEST: state(%s) query err %d\n", when, err);
		return;
	}
	printk("PM-TEST: state(%s) = %s\n", when, pm_state_str(st));
}

static bool state_is(enum pm_device_state expected)
{
	enum pm_device_state st;

	return (pm_device_state_get(VREF_DEV, &st) == 0) && (st == expected);
}
#endif /* CONFIG_PM_DEVICE -- only the PM phases query state, and twister builds
	* with -Werror=unused-function.
	*/

/*
 * Pick a voltage out of the middle of whatever range the variant exposes, so the
 * trim the driver has to remember is chosen the way a consumer would choose it
 * and not hard-coded to one part's register layout.
 */
static int pick_voltage(int32_t *volt_uv)
{
	unsigned int count = regulator_count_voltages(VREF_DEV);

	if (count == 0U) {
		return -ENOTSUP;
	}

	return regulator_list_voltage(VREF_DEV, count / 2U, volt_uv);
}

/* ---- Phase: baseline ----------------------------------------------------- */
static void phase_baseline(void)
{
	int32_t volt_uv = 0;
	int err;

	printk("PM-TEST: phase baseline\n");

	PM_TEST_CHECK(device_is_ready(VREF_DEV), "vref device is ready");
	report_hw("init");

	/*
	 * pm_device_driver_init() runs the TURN_ON action in every build -- with
	 * CONFIG_PM_DEVICE=n its inline stub calls the callback directly -- so
	 * configure_hw() has written these bits by the time main() runs whatever
	 * the PM configuration is.
	 */
	PM_TEST_CHECK((base->CSR & VREF_DT_CSR_BITS) == VREF_DT_CSR_BITS,
		      "CSR carries the devicetree configuration bits at init");
	PM_TEST_CHECK(mode_is(VREF_DT_MODE), "mode matches regulator-initial-mode at init");

	/*
	 * Take a reference and hold it for the rest of the run: from here on the
	 * sample is a consumer that needs the output, which is the situation every
	 * later phase asks about. regulator_enable() spins on CSR[VREFST] itself,
	 * so a bandgap that never stabilises hangs here rather than failing -- read
	 * the last PM-TEST line to tell that apart from a crash.
	 */
	err = regulator_enable(VREF_DEV);
	PM_TEST_CHECK(err == 0, "regulator_enable succeeds");
	PM_TEST_CHECK(vref_output_stable(), "output reads stable once enabled");
	report_hw("enabled");

#if !defined(VREF_DEVICE_PM_PHASE)
	/*
	 * Ask for a voltage, which is what makes the driver latch data->trim_set
	 * and take on the duty of putting that trim back after a reset. The
	 * device-PM phase needs the other branch -- the one that runs when no
	 * consumer has asked -- so in that build this is left to the phase itself,
	 * which exercises both in order.
	 */
	err = pick_voltage(&volt_uv);
	PM_TEST_CHECK(err == 0, "a voltage can be picked out of the trim range");
	if (err == 0) {
		err = regulator_set_voltage(VREF_DEV, volt_uv, volt_uv);
		PM_TEST_CHECK(err == 0, "regulator_set_voltage succeeds");
	}
	report_hw("voltage-set");
#else
	ARG_UNUSED(volt_uv);
#endif
}

/* ---- Phase: manual device PM --------------------------------------------- */
#if defined(VREF_DEVICE_PM_PHASE)
/*
 * Sub-phase A: no consumer has asked for a voltage, so TURN_ON has to put the
 * three configuration bits back and take the trim to the bottom of the range --
 * except on the VREFTRIM-only variant, where the trim register holds a factory
 * value loaded at reset and the driver is right to leave it alone.
 *
 * Clobbering the trim by hand is what stands in for the reset: writing UTRIM
 * directly, rather than through set_voltage(), is deliberate, because
 * set_voltage() would latch trim_set and there would be no untrimmed branch left
 * to observe. On the factory variant this does overwrite the factory trim for the
 * rest of the run; nothing here depends on absolute accuracy.
 */
static void device_cycle_untrimmed(void)
{
	uint32_t after_turn_on;
	int err;

	printk("PM-TEST: device-pm sub-phase: restore with no consumer trim\n");

	report_state("init");
	PM_TEST_CHECK(state_is(PM_DEVICE_STATE_ACTIVE), "device starts ACTIVE");

	base->CSR &= ~VREF_DT_CSR_BITS;
	vref_trim_clobber();
	report_hw("clobbered");

	/* SUSPEND and TURN_OFF are no-ops by design: whether the output is on is
	 * owned by the consumers through the reference count, not by the SoC power
	 * state, so neither may touch the block.
	 */
	err = pm_device_action_run(VREF_DEV, PM_DEVICE_ACTION_SUSPEND);
	PM_TEST_CHECK(err == 0, "SUSPEND accepted");
	PM_TEST_CHECK((base->CSR & VREF_DT_CSR_BITS) == 0U,
		      "SUSPEND leaves the block alone");

	err = pm_device_action_run(VREF_DEV, PM_DEVICE_ACTION_TURN_OFF);
	PM_TEST_CHECK(err == 0, "TURN_OFF accepted");
	PM_TEST_CHECK((base->CSR & VREF_DT_CSR_BITS) == 0U,
		      "TURN_OFF leaves the block alone");

	err = pm_device_action_run(VREF_DEV, PM_DEVICE_ACTION_TURN_ON);
	PM_TEST_CHECK(err == 0, "TURN_ON accepted");
	after_turn_on = base->CSR;
	report_hw("after-turn-on");

	PM_TEST_CHECK((after_turn_on & VREF_DT_CSR_BITS) == VREF_DT_CSR_BITS,
		      "TURN_ON restores the devicetree configuration bits");
#if VREF_TRIM_IS_FACTORY
	PM_TEST_CHECK(vref_trim() == VREF_TRIM_CLOBBER,
		      "TURN_ON leaves the factory trim register alone");
#else
	PM_TEST_CHECK(vref_trim() == 0U,
		      "TURN_ON takes the untrimmed output to the bottom of the range");
#endif

	err = pm_device_action_run(VREF_DEV, PM_DEVICE_ACTION_RESUME);
	PM_TEST_CHECK(err == 0, "RESUME accepted");
	PM_TEST_CHECK(state_is(PM_DEVICE_STATE_ACTIVE), "device is ACTIVE again");
}

/*
 * Sub-phase B: a consumer has asked for a voltage. That trim is the consumer's
 * choice of output and nothing else puts it back once the block has been reset,
 * so TURN_ON has to restore it from the driver's own data -- this is the claim
 * the whole case exists for.
 */
static void device_cycle_trimmed(void)
{
	int32_t chosen_uv = 0;
	int32_t volt_uv = 0;
	int err;

	printk("PM-TEST: device-pm sub-phase: restore a consumer trim\n");

	err = pick_voltage(&chosen_uv);
	PM_TEST_CHECK(err == 0, "a voltage can be picked out of the trim range");
	if (err != 0) {
		return;
	}

	err = regulator_set_voltage(VREF_DEV, chosen_uv, chosen_uv);
	PM_TEST_CHECK(err == 0, "regulator_set_voltage succeeds");
	err = regulator_get_voltage(VREF_DEV, &volt_uv);
	PM_TEST_CHECK(err == 0 && volt_uv == chosen_uv,
		      "regulator_get_voltage reads the voltage back");
	report_hw("voltage-set");

	vref_trim_clobber();
	PM_TEST_CHECK(vref_trim() == VREF_TRIM_CLOBBER, "trim reads back clobbered");

	err = pm_device_action_run(VREF_DEV, PM_DEVICE_ACTION_SUSPEND);
	PM_TEST_CHECK(err == 0, "SUSPEND accepted");
	err = pm_device_action_run(VREF_DEV, PM_DEVICE_ACTION_TURN_OFF);
	PM_TEST_CHECK(err == 0, "TURN_OFF accepted");
	err = pm_device_action_run(VREF_DEV, PM_DEVICE_ACTION_TURN_ON);
	PM_TEST_CHECK(err == 0, "TURN_ON accepted");
	report_hw("after-turn-on");

	err = regulator_get_voltage(VREF_DEV, &volt_uv);
	PM_TEST_CHECK(err == 0 && volt_uv == chosen_uv,
		      "TURN_ON restores the voltage the consumer asked for");

	err = pm_device_action_run(VREF_DEV, PM_DEVICE_ACTION_RESUME);
	PM_TEST_CHECK(err == 0, "RESUME accepted");
	PM_TEST_CHECK(state_is(PM_DEVICE_STATE_ACTIVE), "device is ACTIVE again");
	report_state("after-cycle");
}

static void phase_device_pm(void)
{
	printk("PM-TEST: phase device-pm\n");
	printk("PM-TEST: sequence: clobber, SUSPEND, TURN_OFF, TURN_ON, RESUME\n");

	device_cycle_untrimmed();
	device_cycle_trimmed();

	/*
	 * What TURN_ON does not put back. configure_hw() opens with
	 * regulator_nxp_vref_disable(), so after it has run the bandgap and the
	 * 2.1 V buffer are off and the mode reads STANDBY, however many references
	 * the common layer still counts and whatever regulator-initial-mode said.
	 * Reported rather than checked here: this phase reset nothing, so the
	 * question of who should re-enable the output belongs to the layer where
	 * the block really did lose power. See the system-managed phase and
	 * README.md.
	 */
	printk("PM-TEST: note mode = %s, VREFST = %u, refcount enabled = %u after "
	       "the TURN_ON cycles\n",
	       mode_is(VREF_DT_MODE) ? "as configured" : "not as configured",
	       vref_output_stable() ? 1U : 0U,
	       regulator_is_enabled(VREF_DEV) ? 1U : 0U);
}
#endif /* VREF_DEVICE_PM_PHASE */

/* ---- Phase: runtime device PM -------------------------------------------- */
#if defined(CONFIG_PM_DEVICE_RUNTIME)
static void phase_runtime_pm(void)
{
	int err;

	printk("PM-TEST: phase runtime-pm\n");

	report_state("init");
	/* Two boot states are legal. Without a power domain the device lands in
	 * SUSPENDED: pm_device_driver_init() ran TURN_ON and stopped short of RESUME
	 * because runtime PM is about to take over. With a domain it lands in OFF,
	 * because the domain device is runtime enabled too and is suspended right
	 * after its own init, so pm_device_is_powered() is already false when the
	 * regulator initialises and TURN_ON is skipped. The vref node names
	 * &core_domain, so this is the OFF case, and the hardware never gets the
	 * devicetree configuration -- see the baseline phase and README.md.
	 */
	PM_TEST_CHECK(state_is(PM_DEVICE_STATE_SUSPENDED) || state_is(PM_DEVICE_STATE_OFF),
		      "device starts SUSPENDED or OFF under runtime PM");

	/* The claim of this layer: the reference works while PM calls the device
	 * SUSPENDED. The regulator API takes no runtime PM reference of its own, so
	 * a consumer that enabled the output never asked PM for anything -- and the
	 * driver's SUSPEND is a no-op precisely so that output keeps running.
	 */
	PM_TEST_CHECK(vref_output_stable(), "output stable while not ACTIVE at init");

	err = pm_device_runtime_get(VREF_DEV);
	PM_TEST_CHECK(err == 0, "runtime_get succeeds");
	PM_TEST_CHECK(state_is(PM_DEVICE_STATE_ACTIVE), "device is ACTIVE after runtime_get");

	err = pm_device_runtime_put(VREF_DEV);
	PM_TEST_CHECK(err == 0, "runtime_put succeeds");
	PM_TEST_CHECK(state_is(PM_DEVICE_STATE_SUSPENDED),
		      "device is SUSPENDED after runtime_put");

	PM_TEST_CHECK(vref_output_stable(), "output still stable after runtime_put");
	/* A runtime get resumes the domain before the device, so this is the last
	 * chance for the block to be configured. It is not taken: the domain
	 * forwards TURN_ON only on the system state changes it lists, and an
	 * ordinary resume is not one of them.
	 */
	PM_TEST_CHECK((base->CSR & VREF_DT_CSR_BITS) == VREF_DT_CSR_BITS,
		      "configuration bits present after a runtime get/put cycle");
	report_hw("after-put");
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
 * and says nothing about the reference. Busy-wait, with interrupts on, for long
 * enough that the last character has left and that interrupt has run.
 */
static void console_quiesce(void)
{
	k_busy_wait(5000);
}

static void phase_system_constraints(void)
{
	printk("PM-TEST: phase system-constraints\n");
	printk("PM-TEST: constraints enabled; the vref node carries "
	       "zephyr,disabling-power-states\n");

	/* The driver never calls pm_policy_device_power_lock_get(), and an enabled
	 * reference is not an operation in flight, so there is no window for it to
	 * protect. Declaring the state the block does not survive is still
	 * meaningful -- it is what an application would use to decide whether it
	 * has to re-enable the output -- but nothing may end up holding a policy
	 * lock on the reference's behalf.
	 */
	console_quiesce();
	PM_TEST_CHECK(!pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						      PM_ALL_SUBSTATES),
		      "no constraint held on the reference's behalf");
	report_hw("constraints");
}
#endif /* CONFIG_PM_POLICY_DEVICE_CONSTRAINTS */

/* ---- Phase: the system-managed device sweep ------------------------------ */
#if defined(VREF_SYSTEM_MANAGED_SWEEP)

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
 * Deep Power Down resets every CORE-domain peripheral. The vref is the device
 * under test here and it looks after itself -- the core domain hands it a
 * TURN_ON and it reconfigures. The console driver has no matching hook, so
 * re-initialise it by hand, bottom up: the PORT pin-mux gates first, then the
 * LP_FLEXCOMM parent, then the LPUART. The pin-mux step is redundant on a tree
 * where the pin-mux driver has its own TURN_ON (see samples/port), and is kept so
 * this case does not depend on that half of the work.
 */
#define REINIT_DEVICE(dev)                                                    \
	do {                                                                  \
		(dev)->state->initialized = false;                            \
		(void)device_init(dev);                                       \
	} while (0)

#define REINIT_PORT(node_id) REINIT_DEVICE(DEVICE_DT_GET(node_id));

static void resume_console(void)
{
	DT_FOREACH_STATUS_OKAY(nxp_port_pinmux, REINIT_PORT)

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

static void phase_system_managed(void)
{
	int32_t before_uv = 0;
	int32_t after_uv = 0;
	uint32_t csr_after;
	bool stable_after;
	bool mode_after;
	int err;

	printk("PM-TEST: phase system-managed (%s)\n", TRANSITION_NAME);

	/* No runtime PM in this build, so pm_device_driver_init() resumed the
	 * device and the sweep is the only thing that will suspend it.
	 */
	report_state("init");
	PM_TEST_CHECK(state_is(PM_DEVICE_STATE_ACTIVE),
		      "device starts ACTIVE (system-managed)");
	PM_TEST_CHECK(vref_output_stable(), "output stable before the transition");

	err = regulator_get_voltage(VREF_DEV, &before_uv);
	PM_TEST_CHECK(err == 0, "voltage readable before the transition");
	report_hw("before");

	/* Only the state under test may be entered, and only when this phase asks
	 * for it, so the transition is a single deterministic event and the board
	 * stays awake -- and debuggable -- for the rest of the run.
	 */
	printk("PM-TEST: forcing one %s transition\n", TRANSITION_NAME);
	k_busy_wait(2000); /* let the console finish shifting the line out */

	pm_state_force(0U, &(struct pm_state_info){TRANSITION_STATE, 0U, 0U});
	k_sleep(K_SECONDS(2));

	/* Sample before the console is touched: re-initialising the LPUART takes
	 * time and the interesting question is what the block looked like the
	 * moment the sweep handed it back.
	 */
	csr_after = base->CSR;
	stable_after = vref_output_stable();
	mode_after = mode_is(VREF_DT_MODE);
	err = regulator_get_voltage(VREF_DEV, &after_uv);

	resume_console();
	printk("PM-TEST: resumed from %s\n", TRANSITION_NAME);
	report_hw("after-resume");

	report_state("after-resume");
	PM_TEST_CHECK(state_is(PM_DEVICE_STATE_ACTIVE), "device is ACTIVE again");

	/* What the driver's TURN_ON promises. Under Deep Sleep the block keeps its
	 * registers and these hold trivially; under Deep Power Down they hold only
	 * because configure_hw() ran.
	 */
	PM_TEST_CHECK((csr_after & VREF_DT_CSR_BITS) == VREF_DT_CSR_BITS,
		      "configuration bits restored after the transition");
	PM_TEST_CHECK(err == 0 && after_uv == before_uv,
		      "the consumer's voltage survives the transition");

	/*
	 * What it does not promise. The reference count still says a consumer needs
	 * the output, but configure_hw() opens with regulator_nxp_vref_disable() and
	 * nothing re-applies regulator-initial-mode, so on a block that really lost
	 * power the consumer is left holding an enabled reference over a dead
	 * bandgap and gets no error to tell it so. Whether that is the driver's job
	 * or the consumer's is arguable -- see README.md -- so these two are checked
	 * where the answer matters, and this is the only layer that reaches it.
	 */
	PM_TEST_CHECK(regulator_is_enabled(VREF_DEV),
		      "the reference count still shows the consumer's reference");
	PM_TEST_CHECK(mode_after, "regulator-initial-mode restored after the transition");
	PM_TEST_CHECK(stable_after, "output stable after the transition for a held reference");

	/*
	 * Recovery, whichever way those went: drop the reference and take it again.
	 * A stable output afterwards separates "the automatic restore has a gap"
	 * from "the block came back broken", which are very different findings.
	 */
	err = regulator_disable(VREF_DEV);
	PM_TEST_CHECK(err == 0, "regulator_disable succeeds after the transition");
	err = regulator_enable(VREF_DEV);
	PM_TEST_CHECK(err == 0, "regulator_enable succeeds after the transition");
	PM_TEST_CHECK(vref_output_stable(),
		      "output stable again after a disable/enable cycle");
	report_hw("recovered");
}
#endif /* VREF_SYSTEM_MANAGED_SWEEP */

int main(void)
{
	printk("PM-TEST: BEGIN vref device-pm on %s\n", CONFIG_BOARD_TARGET);
	printk("PM-TEST: device under test: %s at 0x%08x\n", VREF_DEV->name,
	       (unsigned int)DT_REG_ADDR(VREF_NODE));
	printk("PM-TEST: devicetree CSR bits 0x%08x, initial mode %s, trim variant %s\n",
	       (unsigned int)VREF_DT_CSR_BITS, mode_str(VREF_DT_MODE),
	       VREF_TRIM_IS_FACTORY ? "VREFTRIM (factory)" : "TRIM2V1");

	phase_baseline();

#if defined(VREF_DEVICE_PM_PHASE)
	phase_device_pm();
#endif
#if defined(CONFIG_PM_DEVICE_RUNTIME)
	phase_runtime_pm();
#endif
#if defined(CONFIG_PM_POLICY_DEVICE_CONSTRAINTS)
	phase_system_constraints();
#endif
#if defined(VREF_SYSTEM_MANAGED_SWEEP)
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
