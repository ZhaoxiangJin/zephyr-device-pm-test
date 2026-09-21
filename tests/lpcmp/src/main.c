/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 *
 * Device PM test harness for the NXP LPCMP driver
 * (drivers/comparator/comparator_nxp_lpcmp.c).
 *
 * The same source adapts to the PM layer selected at build time:
 *
 *   (baseline)                    no CONFIG_PM_DEVICE      -> API sanity only
 *   CONFIG_PM_DEVICE              manual suspend/resume via pm_device_action_run()
 *   CONFIG_PM_DEVICE_RUNTIME      get/put reference counting
 *   CONFIG_PM (+ constraints)     device power-state constraints on &lpcmp0
 *   CONFIG_PM_TEST_LPCMP_LOOPBACK optional: assert on the real comparator output
 *
 * The LPCMP PM callback's only hardware effect is one bit, CCR0.CMP_EN. So every
 * transition is checked against that bit read straight out of the peripheral, which
 * is what catches a PM state machine that has drifted away from the hardware. The
 * optional loopback layer additionally proves the analog comparison really stopped.
 *
 * Every phase prints a line prefixed "PM-TEST:". The run ends with
 * "PM-TEST: RESULT PASS" or "PM-TEST: RESULT FAIL".
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/comparator.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <soc.h> /* LPCMP_Type, LPCMP_CCR0_CMP_EN_MASK, LPCMP_CCR2_* */

#if defined(CONFIG_PM_TEST_LPCMP_LOOPBACK)
#include <zephyr/drivers/gpio.h>
#endif

#if !DT_NODE_EXISTS(DT_ALIAS(test_comp))
#error "No suitable devicetree overlay specified (need a test-comp alias)"
#endif

#define LPCMP_NODE DT_ALIAS(test_comp)

static const struct device *const lpcmp_dev = DEVICE_DT_GET(LPCMP_NODE);

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

/* ---- Hardware-truth helpers ---------------------------------------------- */

static const LPCMP_Type *lpcmp_regs(void)
{
	return (const LPCMP_Type *)DT_REG_ADDR(LPCMP_NODE);
}

/*
 * The comparator is only actually comparing while CCR0.CMP_EN is set. This is the
 * single bit the driver's PM callback touches, so it is the ground truth against
 * which pm_device_state_get() is judged.
 */
static bool cmp_en(void)
{
	return (lpcmp_regs()->CCR0 & LPCMP_CCR0_CMP_EN_MASK) != 0U;
}

/* CCR2 holds the input mux, hysteresis and power-mode configuration. The driver has
 * no save/restore, so comparing this across a suspend/resume round trip shows whether
 * configuration survives on its own.
 */
static uint32_t cmp_ccr2(void)
{
	return lpcmp_regs()->CCR2;
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

/* Print the PM state and the hardware bit side by side, so a mismatch between what
 * Zephyr believes and what the peripheral is doing is visible on one line.
 */
static void report_state(const char *when)
{
	enum pm_device_state st;
	int err = pm_device_state_get(lpcmp_dev, &st);

	if (err < 0) {
		printk("PM-TEST: state(%s) query err %d, CCR0.CMP_EN=%u\n", when, err,
		       (unsigned)cmp_en());
		return;
	}
	printk("PM-TEST: state(%s) = %s, CCR0.CMP_EN=%u\n", when, pm_state_str(st),
	       (unsigned)cmp_en());
}

static bool state_is(enum pm_device_state expected)
{
	enum pm_device_state st;

	return pm_device_state_get(lpcmp_dev, &st) == 0 && st == expected;
}
#endif /* CONFIG_PM_DEVICE */

/* ---- Observable operation ------------------------------------------------ */

/*
 * One comparator API read. Note this cannot fail because the block is disabled:
 * comparator_get_output() just returns the CSR.COUT latch, so on a suspended device
 * it yields a stale value rather than an error. That is exactly why the register and
 * loopback checks exist.
 */
static int exercise_get_output(void)
{
	return comparator_get_output(lpcmp_dev);
}

/* ---- Optional GPIO loopback --------------------------------------------- */

#if defined(CONFIG_PM_TEST_LPCMP_LOOPBACK)

#if !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), test_gpios)
#error "Loopback layer needs a zephyr,user test-gpios property"
#endif

static const struct gpio_dt_spec loopback_gpio =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), test_gpios);

#define LOOPBACK_SETTLE_MS 50

/* Drive the comparator's positive input to @level and wait for the output to follow.
 * Returns true if it followed within LOOPBACK_SETTLE_MS.
 */
static bool loopback_output_follows(int level)
{
	gpio_pin_set_dt(&loopback_gpio, level);

	for (int i = 0; i < LOOPBACK_SETTLE_MS; i++) {
		if (comparator_get_output(lpcmp_dev) == level) {
			return true;
		}
		k_msleep(1);
	}
	return false;
}

/* Full high/low sweep of the input. Returns true only if the output tracked both
 * edges -- i.e. the comparator is really comparing.
 */
static bool loopback_tracks_input(void)
{
	bool high_ok = loopback_output_follows(1);
	bool low_ok = loopback_output_follows(0);

	return high_ok && low_ok;
}

static int loopback_init(void)
{
	if (!gpio_is_ready_dt(&loopback_gpio)) {
		printk("PM-TEST: loopback gpio %s not ready\n", loopback_gpio.port->name);
		return -ENODEV;
	}
	return gpio_pin_configure_dt(&loopback_gpio, GPIO_OUTPUT_INACTIVE);
}
#endif /* CONFIG_PM_TEST_LPCMP_LOOPBACK */

/* ---- Phase: baseline sanity (always run) --------------------------------- */
static void phase_baseline(void)
{
	int out;
	int err;

	printk("PM-TEST: phase baseline\n");

	out = exercise_get_output();
	PM_TEST_CHECK(out == 0 || out == 1, "get_output returns a valid level");
	printk("PM-TEST: baseline output=%d CCR0.CMP_EN=%u CCR2=0x%08x\n", out,
	       (unsigned)cmp_en(), cmp_ccr2());

	err = comparator_set_trigger(lpcmp_dev, COMPARATOR_TRIGGER_BOTH_EDGES);
	PM_TEST_CHECK(err == 0, "set_trigger(BOTH_EDGES) returns 0");

	err = comparator_trigger_is_pending(lpcmp_dev);
	PM_TEST_CHECK(err >= 0, "trigger_is_pending does not error");

	err = comparator_set_trigger(lpcmp_dev, COMPARATOR_TRIGGER_NONE);
	PM_TEST_CHECK(err == 0, "set_trigger(NONE) returns 0");

#if defined(CONFIG_PM_TEST_LPCMP_LOOPBACK)
	PM_TEST_CHECK(loopback_tracks_input(), "baseline: output tracks looped-back GPIO");
#endif
}

/* ---- Phase: manual device PM (CONFIG_PM_DEVICE) -------------------------- */
#if defined(CONFIG_PM_DEVICE) && !defined(CONFIG_PM_DEVICE_RUNTIME)
static void phase_device_pm(void)
{
	uint32_t ccr2_before;
	int err;

	printk("PM-TEST: phase device-pm\n");

	/* Without runtime PM, pm_device_driver_init() resumes the device. */
	report_state("init");
	PM_TEST_CHECK(state_is(PM_DEVICE_STATE_ACTIVE),
		      "device starts ACTIVE (no runtime PM)");
	PM_TEST_CHECK(cmp_en(), "CMP_EN set at init");

	ccr2_before = cmp_ccr2();

	/* Suspend: the callback must actually disable the comparator. */
	err = pm_device_action_run(lpcmp_dev, PM_DEVICE_ACTION_SUSPEND);
	PM_TEST_CHECK(err == 0, "SUSPEND action returns 0");
	report_state("after-suspend");
	PM_TEST_CHECK(state_is(PM_DEVICE_STATE_SUSPENDED),
		      "device is SUSPENDED after SUSPEND");
	PM_TEST_CHECK(!cmp_en(), "CMP_EN cleared by SUSPEND");

	printk("PM-TEST: read-while-suspended output=%d (stale CSR.COUT, not an error)\n",
	       exercise_get_output());

#if defined(CONFIG_PM_TEST_LPCMP_LOOPBACK)
	PM_TEST_CHECK(!loopback_tracks_input(),
		      "suspended: output does NOT track looped-back GPIO");
	report_state("after-suspended-loopback");
#endif

	/*
	 * Known driver defect: nxp_lpcmp_set_trigger_callback() clears CMP_EN, updates
	 * the callback, then unconditionally sets CMP_EN again without consulting the
	 * PM state. Calling it on a suspended device therefore re-enables the hardware
	 * behind PM's back. Expected to FAIL until the driver is fixed.
	 */
	err = comparator_set_trigger_callback(lpcmp_dev, NULL, NULL);
	printk("PM-TEST: set_trigger_callback-while-suspended err=%d\n", err);
	report_state("after-set-callback-while-suspended");
	PM_TEST_CHECK(!cmp_en(),
		      "set_trigger_callback does not re-enable a SUSPENDED comparator");

	/* Resume and confirm the comparator is enabled again. */
	err = pm_device_action_run(lpcmp_dev, PM_DEVICE_ACTION_RESUME);
	PM_TEST_CHECK(err == 0, "RESUME action returns 0");
	report_state("after-resume");
	PM_TEST_CHECK(state_is(PM_DEVICE_STATE_ACTIVE), "device is ACTIVE after RESUME");
	PM_TEST_CHECK(cmp_en(), "CMP_EN set after RESUME");

	PM_TEST_CHECK(cmp_ccr2() == ccr2_before,
		      "CCR2 config survives the suspend/resume round trip");
	printk("PM-TEST: CCR2 before=0x%08x after=0x%08x\n", ccr2_before, cmp_ccr2());

#if defined(CONFIG_PM_TEST_LPCMP_LOOPBACK)
	PM_TEST_CHECK(loopback_tracks_input(), "resumed: output tracks looped-back GPIO");
#endif

	/* Double resume should be rejected, not crash. */
	err = pm_device_action_run(lpcmp_dev, PM_DEVICE_ACTION_RESUME);
	PM_TEST_CHECK(err == -EALREADY, "double RESUME rejected with -EALREADY");

	/* The driver implements only SUSPEND/RESUME; TURN_OFF is unimplemented. */
	err = pm_device_action_run(lpcmp_dev, PM_DEVICE_ACTION_TURN_OFF);
	printk("PM-TEST: TURN_OFF action err=%d (driver implements SUSPEND/RESUME only)\n",
	       err);
}
#endif /* CONFIG_PM_DEVICE && !CONFIG_PM_DEVICE_RUNTIME */

/* ---- Phase: runtime device PM (CONFIG_PM_DEVICE_RUNTIME) ----------------- */
#if defined(CONFIG_PM_DEVICE_RUNTIME)
static void phase_runtime_pm(void)
{
	int err;

	printk("PM-TEST: phase runtime-pm\n");

	/*
	 * With runtime PM auto-enabled, pm_device_driver_init() sets the state to
	 * SUSPENDED and returns early -- it never runs the SUSPEND callback. But
	 * nxp_lpcmp_init() has already set CCR0.CMP_EN one line earlier, so the
	 * comparator boots powered up while PM believes it is suspended.
	 *
	 * Known driver defect: the CMP_EN check below is expected to FAIL until
	 * nxp_lpcmp_init() stops enabling the block ahead of pm_device_driver_init().
	 */
	report_state("init");
	PM_TEST_CHECK(state_is(PM_DEVICE_STATE_SUSPENDED),
		      "device starts SUSPENDED (runtime PM)");
	PM_TEST_CHECK(!cmp_en(), "CMP_EN cleared at init to match SUSPENDED state");

	/* The driver takes no runtime reference of its own, so an unwrapped API call
	 * neither resumes the device nor fails; it just reads a stale latch.
	 */
	printk("PM-TEST: unwrapped-read output=%d (driver does not auto-get)\n",
	       exercise_get_output());
	report_state("after-unwrapped-read");

	/* Correct usage: the caller brackets use with get/put. */
	err = pm_device_runtime_get(lpcmp_dev);
	PM_TEST_CHECK(err == 0, "runtime_get returns 0");
	report_state("after-get");
	PM_TEST_CHECK(state_is(PM_DEVICE_STATE_ACTIVE), "device ACTIVE after runtime_get");
	PM_TEST_CHECK(cmp_en(), "CMP_EN set after runtime_get");

#if defined(CONFIG_PM_TEST_LPCMP_LOOPBACK)
	PM_TEST_CHECK(loopback_tracks_input(), "held ACTIVE: output tracks looped-back GPIO");
#endif

	/* Nested references: the device must stay ACTIVE until the last put. */
	err = pm_device_runtime_get(lpcmp_dev);
	PM_TEST_CHECK(err == 0, "nested runtime_get returns 0");
	err = pm_device_runtime_put(lpcmp_dev);
	PM_TEST_CHECK(err == 0, "first runtime_put returns 0");
	report_state("after-nested-put");
	PM_TEST_CHECK(state_is(PM_DEVICE_STATE_ACTIVE),
		      "device still ACTIVE while a reference is held");
	PM_TEST_CHECK(cmp_en(), "CMP_EN still set while a reference is held");

	err = pm_device_runtime_put(lpcmp_dev);
	PM_TEST_CHECK(err == 0, "final runtime_put returns 0");
	report_state("after-put");
	PM_TEST_CHECK(state_is(PM_DEVICE_STATE_SUSPENDED),
		      "device SUSPENDED after last runtime_put");
	PM_TEST_CHECK(!cmp_en(), "CMP_EN cleared after last runtime_put");

#if defined(CONFIG_PM_TEST_LPCMP_LOOPBACK)
	PM_TEST_CHECK(!loopback_tracks_input(),
		      "runtime-suspended: output does NOT track looped-back GPIO");
#endif
}
#endif /* CONFIG_PM_DEVICE_RUNTIME */

/* ---- Phase: system PM device constraints (CONFIG_PM_POLICY_DEVICE_CONSTRAINTS) */
#if defined(CONFIG_PM_POLICY_DEVICE_CONSTRAINTS)
static void phase_system_constraints(void)
{
	int out;

	printk("PM-TEST: phase system-constraints\n");
	printk("PM-TEST: constraints enabled; &lpcmp0 zephyr,disabling-power-states "
	       "lists the states that cut the comparator's analog bias\n");
	printk("PM-TEST: note the LPCMP driver never calls pm_policy_device_power_lock_get() "
	       "itself, so the application must hold the lock while it needs the output\n");

#if defined(CONFIG_PM_DEVICE_RUNTIME)
	PM_TEST_CHECK(pm_device_runtime_get(lpcmp_dev) == 0,
		      "runtime_get before constrained read");
#endif
	out = exercise_get_output();
	PM_TEST_CHECK(out == 0 || out == 1, "constrained read returns a valid level");
	printk("PM-TEST: constrained output=%d CCR0.CMP_EN=%u\n", out, (unsigned)cmp_en());
	PM_TEST_CHECK(cmp_en(), "CMP_EN set for the constrained read");
#if defined(CONFIG_PM_DEVICE_RUNTIME)
	(void)pm_device_runtime_put(lpcmp_dev);
#endif
}
#endif /* CONFIG_PM_POLICY_DEVICE_CONSTRAINTS */

int main(void)
{
	printk("PM-TEST: BEGIN lpcmp device-pm on %s\n", CONFIG_BOARD_TARGET);

	if (!device_is_ready(lpcmp_dev)) {
		printk("PM-TEST: %s not ready\n", lpcmp_dev->name);
		printk("PM-TEST: RESULT FAIL\n");
		return 0;
	}
	printk("PM-TEST: device %s at 0x%08x\n", lpcmp_dev->name,
	       (unsigned)DT_REG_ADDR(LPCMP_NODE));

#if defined(CONFIG_PM_TEST_LPCMP_LOOPBACK)
	if (loopback_init() < 0) {
		printk("PM-TEST: loopback layer needs a jumper J2-11 (gpio1.12) -> "
		       "J2-17 (CMP0_IN0)\n");
		printk("PM-TEST: RESULT FAIL\n");
		return 0;
	}
	printk("PM-TEST: loopback layer active on %s pin %u\n", loopback_gpio.port->name,
	       loopback_gpio.pin);
#endif

	phase_baseline();

#if defined(CONFIG_PM_DEVICE) && !defined(CONFIG_PM_DEVICE_RUNTIME)
	phase_device_pm();
#endif
#if defined(CONFIG_PM_DEVICE_RUNTIME)
	phase_runtime_pm();
#endif
#if defined(CONFIG_PM_POLICY_DEVICE_CONSTRAINTS)
	phase_system_constraints();
#endif

	printk("PM-TEST: RESULT %s\n", test_failed ? "FAIL" : "PASS");
	return 0;
}
