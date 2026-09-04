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

#if defined(CONFIG_PM_DEVICE_RUNTIME)
	/* Runtime PM is genuinely enabled here, so the device boots SUSPENDED and this
	 * driver takes no runtime reference of its own. The control read therefore has to
	 * hold one; the unwrapped case is what phase_runtime_pm() documents on purpose.
	 */
	err = pm_device_runtime_get(LPADC_DEV);
	PM_TEST_CHECK(err == 0, "runtime_get for baseline read");
#endif
	err = exercise_read(&raw);
	PM_TEST_CHECK(err == 0, "baseline read succeeds");
	if (err == 0) {
		printk("PM-TEST: baseline raw=%d\n", raw);
	} else {
		printk("PM-TEST: baseline read err %d\n", err);
	}
#if defined(CONFIG_PM_DEVICE_RUNTIME)
	(void)pm_device_runtime_put(LPADC_DEV);
#endif
}

/* ---- Phase: manual device PM (CONFIG_PM_DEVICE) -------------------------- */
#if defined(CONFIG_PM_DEVICE) && !defined(CONFIG_PM_DEVICE_RUNTIME)
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
#endif /* CONFIG_PM_DEVICE && !CONFIG_PM_DEVICE_RUNTIME */

/* ---- Phase: runtime device PM (CONFIG_PM_DEVICE_RUNTIME) ----------------- */
#if defined(CONFIG_PM_DEVICE_RUNTIME)
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

	/* NOTE: the LPADC read path does NOT call pm_device_runtime_get/put itself.
	 * Document current behavior: an unwrapped read against a runtime-suspended
	 * device does not auto-resume it.
	 */
	err = exercise_read(&raw);
	printk("PM-TEST: unwrapped-read err=%d raw=%d (driver does not auto-get)\n",
	       err, raw);
	report_state("after-unwrapped-read");

	/* Correct usage: caller wraps the read in get/put. */
	err = pm_device_runtime_get(LPADC_DEV);
	PM_TEST_CHECK(err == 0, "runtime_get returns 0");
	report_state("after-get");
	err = pm_device_state_get(LPADC_DEV, &st);
	PM_TEST_CHECK(err == 0 && st == PM_DEVICE_STATE_ACTIVE,
		      "device ACTIVE after runtime_get");

	err = exercise_read(&raw);
	PM_TEST_CHECK(err == 0, "wrapped read succeeds");
	printk("PM-TEST: wrapped raw=%d\n", raw);

	err = pm_device_runtime_put(LPADC_DEV);
	PM_TEST_CHECK(err == 0, "runtime_put returns 0");
	report_state("after-put");
	err = pm_device_state_get(LPADC_DEV, &st);
	PM_TEST_CHECK(err == 0 && st == PM_DEVICE_STATE_SUSPENDED,
		      "device SUSPENDED after runtime_put");
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
	 * releases it on completion. We can only observe that reads still work
	 * with constraints compiled in; the lock balance is exercised internally.
	 */
#if defined(CONFIG_PM_DEVICE_RUNTIME)
	err = pm_device_runtime_get(LPADC_DEV);
	PM_TEST_CHECK(err == 0, "runtime_get before constrained read");
#endif
	err = exercise_read(&raw);
	PM_TEST_CHECK(err == 0, "constrained read succeeds");
	printk("PM-TEST: constrained raw=%d\n", raw);
#if defined(CONFIG_PM_DEVICE_RUNTIME)
	(void)pm_device_runtime_put(LPADC_DEV);
#endif
}
#endif /* CONFIG_PM_POLICY_DEVICE_CONSTRAINTS */

int main(void)
{
	printk("PM-TEST: BEGIN lpadc device-pm on %s\n", CONFIG_BOARD_TARGET);

	if (setup_channels() < 0) {
		printk("PM-TEST: RESULT FAIL\n");
		return 0;
	}

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
