/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 *
 * Device PM test harness for the NXP LPADC driver (drivers/adc/adc_mcux_lpadc.c).
 *
 * The same source adapts to the PM layer selected at build time:
 *
 *   (baseline)                    no CONFIG_PM_DEVICE      -> read sanity only
 *   CONFIG_PM_DEVICE              manual suspend/resume via pm_device_action_run()
 *   CONFIG_PM_DEVICE_RUNTIME      get/put reference counting
 *   CONFIG_PM (+ constraints)     device power-state constraints on &lpadc0
 *   CONFIG_PM_DEVICE_SYSTEM_MANAGED  the system device sweep, across one forced
 *                                    Deep Sleep transition
 *   + CONFIG_PM_S2RAM and &deeppowerdown enabled: the same, across Deep Power
 *     Down, which is the only state that takes the register block down and
 *     therefore the only one that reaches the driver's TURN_OFF/TURN_ON
 *
 * Every phase prints a line prefixed "PM-TEST:". The run ends with
 * "PM-TEST: RESULT PASS" or "PM-TEST: RESULT FAIL".
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
	!DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified (need zephyr,user io-channels)"
#endif

#define DT_SPEC_AND_COMMA_FOR_INPUTS(node_id, prop, idx) \
	COND_CODE_1(DT_PHA_HAS_CELL_AT_IDX(node_id, prop, idx, input), \
		    (ADC_DT_SPEC_GET_BY_IDX(node_id, idx),), ())

static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels,
			     DT_SPEC_AND_COMMA_FOR_INPUTS)
};

/* The LPADC controller device (parent of the io-channels). */
#define LPADC_DEV (adc_channels[0].dev)

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

/*
 * Perform one observable ADC read on channel 0. Returns 0 on a successful
 * conversion, negative errno otherwise. The raw sample is stored in *out_raw.
 *
 * Whether a read "works" is exactly what distinguishes an ACTIVE device from a
 * SUSPENDED one: the driver's SUSPEND action calls LPADC_Enable(false), so a
 * conversion started against a suspended block will not complete normally.
 */
static int exercise_read(int32_t *out_raw)
{
	int16_t buf;
	struct adc_sequence sequence = {
		.buffer = &buf,
		.buffer_size = sizeof(buf),
	};
	int err;

	err = adc_sequence_init_dt(&adc_channels[0], &sequence);
	if (err < 0) {
		return err;
	}

	err = adc_read_dt(&adc_channels[0], &sequence);
	if (err < 0) {
		return err;
	}

	*out_raw = buf;
	return 0;
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
	int err = pm_device_state_get(LPADC_DEV, &st);

	if (err < 0) {
		printk("PM-TEST: state(%s) query err %d\n", when, err);
		return;
	}
	printk("PM-TEST: state(%s) = %s\n", when, pm_state_str(st));
}
#endif /* CONFIG_PM_DEVICE -- only the PM phases query state, and twister builds
	* with -Werror=unused-function.
	*/

static int setup_channels(void)
{
	for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {
		if (!adc_is_ready_dt(&adc_channels[i])) {
			printk("PM-TEST: ADC %s not ready\n", adc_channels[i].dev->name);
			return -ENODEV;
		}
		int err = adc_channel_setup_dt(&adc_channels[i]);

		if (err < 0) {
			printk("PM-TEST: channel %u setup err %d\n", (unsigned)i, err);
			return err;
		}
	}
	return 0;
}

/* ---- Phase: baseline sanity (always run) --------------------------------- */
static void phase_baseline(void)
{
	int32_t raw = 0;
	int err;

	printk("PM-TEST: phase baseline\n");

	/*
	 * With runtime PM the device boots SUSPENDED, but the read path takes its
	 * own runtime reference, so this control read needs no wrapping either way.
	 * phase_runtime_pm() asserts that in detail.
	 */
	err = exercise_read(&raw);
	PM_TEST_CHECK(err == 0, "baseline read succeeds");
	if (err == 0) {
		printk("PM-TEST: baseline raw=%d\n", raw);
	} else {
		printk("PM-TEST: baseline read err %d\n", err);
	}
}

/*
 * PM_DEVICE_SYSTEM_MANAGED defaults on whenever device PM is enabled without
 * runtime PM -- even with no CONFIG_PM at all -- so the symbol on its own does
 * not mean a suspend sweep exists. Only CONFIG_PM brings one, and that is what
 * decides which of the two phases below owns the no-runtime-PM case.
 */
#if defined(CONFIG_PM) && defined(CONFIG_PM_DEVICE_SYSTEM_MANAGED)
#define LPADC_SYSTEM_MANAGED_SWEEP 1
#endif

/* ---- Phase: manual device PM (CONFIG_PM_DEVICE) -------------------------- */
#if defined(CONFIG_PM_DEVICE) && !defined(CONFIG_PM_DEVICE_RUNTIME) && \
    !defined(LPADC_SYSTEM_MANAGED_SWEEP)
static void phase_device_pm(void)
{
	int32_t raw = 0;
	int err;
	enum pm_device_state st;

	printk("PM-TEST: phase device-pm\n");

	/* Without runtime PM, pm_device_driver_init() resumes the device. */
	report_state("init");
	err = pm_device_state_get(LPADC_DEV, &st);
	PM_TEST_CHECK(err == 0 && st == PM_DEVICE_STATE_ACTIVE,
		      "device starts ACTIVE (no runtime PM)");

	/* Suspend, then attempt a read: the block is disabled, so this documents
	 * how the driver behaves against a suspended peripheral.
	 */
	err = pm_device_action_run(LPADC_DEV, PM_DEVICE_ACTION_SUSPEND);
	PM_TEST_CHECK(err == 0, "SUSPEND action returns 0");
	report_state("after-suspend");
	err = pm_device_state_get(LPADC_DEV, &st);
	PM_TEST_CHECK(err == 0 && st == PM_DEVICE_STATE_SUSPENDED,
		      "device is SUSPENDED after SUSPEND");

	err = exercise_read(&raw);
	PM_TEST_CHECK(err == -EBUSY, "read while SUSPENDED is rejected with -EBUSY");
	printk("PM-TEST: read-while-suspended err=%d raw=%d\n", err, raw);

	/* Resume and confirm reads work again. */
	err = pm_device_action_run(LPADC_DEV, PM_DEVICE_ACTION_RESUME);
	PM_TEST_CHECK(err == 0, "RESUME action returns 0");
	report_state("after-resume");
	err = pm_device_state_get(LPADC_DEV, &st);
	PM_TEST_CHECK(err == 0 && st == PM_DEVICE_STATE_ACTIVE,
		      "device is ACTIVE after RESUME");

	err = exercise_read(&raw);
	PM_TEST_CHECK(err == 0, "read after RESUME succeeds");
	printk("PM-TEST: post-resume raw=%d\n", raw);

	/* Double-suspend / double-resume should be rejected, not crash. */
	err = pm_device_action_run(LPADC_DEV, PM_DEVICE_ACTION_RESUME);
	PM_TEST_CHECK(err == -EALREADY, "double RESUME rejected with -EALREADY");
}
#endif /* CONFIG_PM_DEVICE && !CONFIG_PM_DEVICE_RUNTIME &&
	* !LPADC_SYSTEM_MANAGED_SWEEP
	*/

/* ---- Phase: runtime device PM (CONFIG_PM_DEVICE_RUNTIME) ----------------- */
#if defined(CONFIG_PM_DEVICE_RUNTIME)
/*
 * The reference the read path takes is dropped with pm_device_runtime_put_async()
 * from the completion interrupt, so the suspend itself runs later on the system
 * work queue. Give it a chance to run before sampling the state.
 */
static void settle_async_suspend(void)
{
	k_msleep(20);
}

static void phase_runtime_pm(void)
{
	int32_t raw = 0;
	int err;
	enum pm_device_state st;

	printk("PM-TEST: phase runtime-pm\n");

	/* With runtime PM, pm_device_driver_init() leaves the device SUSPENDED. */
	report_state("init");
	err = pm_device_state_get(LPADC_DEV, &st);
	PM_TEST_CHECK(err == 0 && st == PM_DEVICE_STATE_SUSPENDED,
		      "device starts SUSPENDED (runtime PM)");

	/* The read path holds a runtime reference for the length of the sequence,
	 * so a plain read resumes the converter, converts, and lets it go again.
	 */
	err = exercise_read(&raw);
	PM_TEST_CHECK(err == 0, "unwrapped read auto-resumes and succeeds");
	printk("PM-TEST: unwrapped raw=%d\n", raw);

	settle_async_suspend();
	report_state("after-unwrapped-read");
	err = pm_device_state_get(LPADC_DEV, &st);
	PM_TEST_CHECK(err == 0 && st == PM_DEVICE_STATE_SUSPENDED,
		      "device back to SUSPENDED after the read");

	/* A caller that holds its own reference across several reads keeps the
	 * converter up, and the reads must not disturb that reference.
	 */
	err = pm_device_runtime_get(LPADC_DEV);
	PM_TEST_CHECK(err == 0, "runtime_get returns 0");
	report_state("after-get");
	err = pm_device_state_get(LPADC_DEV, &st);
	PM_TEST_CHECK(err == 0 && st == PM_DEVICE_STATE_ACTIVE,
		      "device ACTIVE after runtime_get");

	err = exercise_read(&raw);
	PM_TEST_CHECK(err == 0, "wrapped read succeeds");
	printk("PM-TEST: wrapped raw=%d\n", raw);

	settle_async_suspend();
	err = pm_device_state_get(LPADC_DEV, &st);
	PM_TEST_CHECK(err == 0 && st == PM_DEVICE_STATE_ACTIVE,
		      "caller's reference survives the read");

	err = pm_device_runtime_put(LPADC_DEV);
	PM_TEST_CHECK(err == 0, "runtime_put returns 0");
	report_state("after-put");
	err = pm_device_state_get(LPADC_DEV, &st);
	PM_TEST_CHECK(err == 0 && st == PM_DEVICE_STATE_SUSPENDED,
		      "device SUSPENDED after runtime_put");

	/* channel_setup() must not touch the reference supplies while the device
	 * is suspended: doing so would leave the regulator's use count high for
	 * good. All it may do is record what RESUME has to apply.
	 */
	err = setup_channels();
	PM_TEST_CHECK(err == 0, "channel_setup while SUSPENDED returns 0");
	err = pm_device_state_get(LPADC_DEV, &st);
	PM_TEST_CHECK(err == 0 && st == PM_DEVICE_STATE_SUSPENDED,
		      "channel_setup did not resume the device");

	err = exercise_read(&raw);
	PM_TEST_CHECK(err == 0, "read after suspended channel_setup succeeds");
	printk("PM-TEST: post-setup raw=%d\n", raw);
	settle_async_suspend();
}
#endif /* CONFIG_PM_DEVICE_RUNTIME */

/* ---- Phase: system PM device constraints (CONFIG_PM_POLICY_DEVICE_CONSTRAINTS) */
#if defined(CONFIG_PM_POLICY_DEVICE_CONSTRAINTS)
static void phase_system_constraints(void)
{
	int32_t raw = 0;
	int err;

	printk("PM-TEST: phase system-constraints\n");
	printk("PM-TEST: constraints enabled; &lpadc0 zephyr,disabling-power-states "
	       "gates the disabling state around a conversion\n");

	/* The driver takes a pm_policy_device_power_lock around each read and
	 * releases it on completion. The lock is observable through the policy
	 * API, so both halves of that pair can be checked: nothing may be held
	 * before the read, and nothing may be left behind after it. A leaked lock
	 * would block the listed states for the rest of the run.
	 */
	PM_TEST_CHECK(!pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						      PM_ALL_SUBSTATES),
		      "no constraint held before the read");

	err = exercise_read(&raw);
	PM_TEST_CHECK(err == 0, "constrained read succeeds");
	printk("PM-TEST: constrained raw=%d\n", raw);

	PM_TEST_CHECK(!pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						      PM_ALL_SUBSTATES),
		      "constraint released after the read");
}
#endif /* CONFIG_PM_POLICY_DEVICE_CONSTRAINTS */

/* ---- Phase: the system-managed device sweep ------------------------------ */
#if defined(LPADC_SYSTEM_MANAGED_SWEEP)

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
 * Deep Power Down resets every CORE-domain peripheral. The LPADC is the device
 * under test here and it looks after itself -- the peripheral domain hands it a
 * TURN_ON and it reconfigures and recalibrates. The console is not on that
 * domain and its driver has no matching hook, so re-initialise it by hand,
 * bottom up: the PORT pin-mux gates first, then the LP_FLEXCOMM parent, then the
 * LPUART. This is the workaround the LPADC no longer needs, kept here only so
 * the test can report its result.
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
	int32_t raw = 0;
	enum pm_device_state st;
	int err;

	printk("PM-TEST: phase system-managed (%s)\n", TRANSITION_NAME);

	/* No runtime PM in this build, so pm_device_driver_init() resumed the
	 * device and the sweep is the only thing that will suspend it.
	 */
	report_state("init");
	err = pm_device_state_get(LPADC_DEV, &st);
	PM_TEST_CHECK(err == 0 && st == PM_DEVICE_STATE_ACTIVE,
		      "device starts ACTIVE (system-managed)");

	err = exercise_read(&raw);
	PM_TEST_CHECK(err == 0, "read before the transition succeeds");

	/* Only the state under test may be entered, and only when this phase asks
	 * for it, so the transition is a single deterministic event and the board
	 * stays awake -- and debuggable -- for the rest of the run.
	 */
	printk("PM-TEST: forcing one %s transition\n", TRANSITION_NAME);
	k_busy_wait(2000); /* let the console finish shifting the line out */

	pm_state_force(0U, &(struct pm_state_info){TRANSITION_STATE, 0U, 0U});
	k_sleep(K_SECONDS(2));

	resume_console();
	printk("PM-TEST: resumed from %s\n", TRANSITION_NAME);

	/* pm_resume_devices() walks the devices it suspended in init order, so the
	 * peripheral domain's TURN_ON has already run by the time the LPADC's own
	 * RESUME does. Both together have to leave a converter that works.
	 */
	report_state("after-resume");
	err = pm_device_state_get(LPADC_DEV, &st);
	PM_TEST_CHECK(err == 0 && st == PM_DEVICE_STATE_ACTIVE,
		      "device is ACTIVE again after the transition");

	err = exercise_read(&raw);
	PM_TEST_CHECK(err == 0, "read after the transition succeeds");
	printk("PM-TEST: post-transition raw=%d\n", raw);
}
#endif /* LPADC_SYSTEM_MANAGED_SWEEP */

int main(void)
{
	printk("PM-TEST: BEGIN lpadc device-pm on %s\n", CONFIG_BOARD_TARGET);

	if (setup_channels() < 0) {
		printk("PM-TEST: RESULT FAIL\n");
		return 0;
	}

	phase_baseline();

#if defined(CONFIG_PM_DEVICE) && !defined(CONFIG_PM_DEVICE_RUNTIME) && \
    !defined(LPADC_SYSTEM_MANAGED_SWEEP)
	phase_device_pm();
#endif
#if defined(CONFIG_PM_DEVICE_RUNTIME)
	phase_runtime_pm();
#endif
#if defined(CONFIG_PM_POLICY_DEVICE_CONSTRAINTS)
	phase_system_constraints();
#endif
#if defined(LPADC_SYSTEM_MANAGED_SWEEP)
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
