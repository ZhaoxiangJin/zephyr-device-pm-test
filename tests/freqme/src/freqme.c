/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_control.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <fsl_inputmux.h>

#include "freqme.h"

/* FREQME register offsets and CTRLSTAT fields, from the reference manual. */
#define FREQME_CTRL_W_OFF   0x0U
#define FREQME_CTRLSTAT_OFF 0x4U
#define FREQME_MIN_OFF      0x8U
#define FREQME_MAX_OFF      0xCU

#define CTRLSTAT_REF_SCALE   GENMASK(4, 0)
#define CTRLSTAT_PULSE_MODE  BIT(8)
#define CTRLSTAT_PULSE_POL   BIT(9)
#define CTRLSTAT_LT_MIN_IE   BIT(12)
#define CTRLSTAT_GT_MAX_IE   BIT(13)
#define CTRLSTAT_READY_IE    BIT(14)
#define CTRLSTAT_CONTINUOUS  BIT(30)
#define CTRLSTAT_IN_PROGRESS BIT(31)

#define CTRLSTAT_INT_ENABLES (CTRLSTAT_LT_MIN_IE | CTRLSTAT_GT_MAX_IE | CTRLSTAT_READY_IE)
#define CTRLSTAT_CONFIG                                                                            \
	(CTRLSTAT_REF_SCALE | CTRLSTAT_PULSE_MODE | CTRLSTAT_PULSE_POL | CTRLSTAT_INT_ENABLES)

/* A devicetree connection cookie is the selector plus the register offset << 20. */
#define ROUTE_SHIFT 20U
#define ROUTE_VALUE(cookie)  ((uint32_t)(cookie) & (BIT(ROUTE_SHIFT) - 1U))
#define ROUTE_OFFSET(cookie) ((uint32_t)(cookie) >> ROUTE_SHIFT)

#define REF_NODE DT_CHILD(FREQME_NODE, reference_sources)
#define TAR_NODE DT_CHILD(FREQME_NODE, target_sources)
#define ALT_REF_NODE DT_CHILD(REF_NODE, ref_fro_12m)

BUILD_ASSERT(DT_NODE_EXISTS(ALT_REF_NODE),
	     "freqme0 has no ref-fro-12m reference source to switch to");

#define COOKIE(node) DT_PHA_BY_IDX(node, mux_states, 0, connection)

/* Exactly one child of each container is the default, so the sum is its cookie. */
#define DEFAULT_COOKIE_ONE(node) +(DT_PROP(node, default_source) ? COOKIE(node) : 0)
#define DEFAULT_REF_COOKIE ((uint32_t)(0 DT_FOREACH_CHILD(REF_NODE, DEFAULT_COOKIE_ONE)))
#define DEFAULT_TAR_COOKIE ((uint32_t)(0 DT_FOREACH_CHILD(TAR_NODE, DEFAULT_COOKIE_ONE)))

#define INPUTMUX_BASE                                                                              \
	((uintptr_t)DT_REG_ADDR(DT_PHANDLE_BY_IDX(ALT_REF_NODE, mux_states, 0)))

static const uintptr_t freqme_base = DT_REG_ADDR(FREQME_NODE);

static K_SEM_DEFINE(measure_done, 0, 1);
static atomic_t measure_events;
static atomic_t window_events;

static inline uint32_t reg_read(uintptr_t addr)
{
	return *(volatile uint32_t *)addr;
}

static inline void reg_write(uintptr_t addr, uint32_t value)
{
	*(volatile uint32_t *)addr = value;
}

static uint32_t ctrlstat(void)
{
	return reg_read(freqme_base + FREQME_CTRLSTAT_OFF);
}

const struct device *freqme_dev(void)
{
	return DEVICE_DT_GET(FREQME_NODE);
}

void freqme_route_read(uint32_t *ref, uint32_t *tar)
{
	INPUTMUX_Init((void *)INPUTMUX_BASE);
	*ref = reg_read(INPUTMUX_BASE + ROUTE_OFFSET(DEFAULT_REF_COOKIE));
	*tar = reg_read(INPUTMUX_BASE + ROUTE_OFFSET(DEFAULT_TAR_COOKIE));
	INPUTMUX_Deinit((void *)INPUTMUX_BASE);
}

void freqme_image_read(struct freqme_image *img)
{
	img->ctrl = ctrlstat() & CTRLSTAT_CONFIG;
	img->min = reg_read(freqme_base + FREQME_MIN_OFF);
	img->max = reg_read(freqme_base + FREQME_MAX_OFF);
	freqme_route_read(&img->route_ref, &img->route_tar);
}

bool freqme_image_equal(const struct freqme_image *a, const struct freqme_image *b)
{
	return (a->ctrl == b->ctrl) && (a->min == b->min) && (a->max == b->max) &&
	       (a->route_ref == b->route_ref) && (a->route_tar == b->route_tar);
}

bool freqme_measuring(void)
{
	return (ctrlstat() & CTRLSTAT_IN_PROGRESS) != 0U;
}

bool freqme_continuous(void)
{
	return (ctrlstat() & CTRLSTAT_CONTINUOUS) != 0U;
}

uint32_t freqme_int_enables(void)
{
	return ctrlstat() & (uint32_t)CTRLSTAT_INT_ENABLES;
}

uint32_t freqme_min(void)
{
	return reg_read(freqme_base + FREQME_MIN_OFF);
}

void freqme_min_write(uint32_t value)
{
	reg_write(freqme_base + FREQME_MIN_OFF, value);
}

void freqme_clobber(struct freqme_image *clobbered)
{
	/* REF_SCALE 31 and no interrupt enable, with MEASURE_IN_PROGRESS 0. No
	 * configuration in this case asks for a window that long.
	 */
	reg_write(freqme_base + FREQME_CTRL_W_OFF, CTRLSTAT_REF_SCALE);
	reg_write(freqme_base + FREQME_MIN_OFF, 0x00ABCDEFU);
	reg_write(freqme_base + FREQME_MAX_OFF, 0x00FEDCBAU);
	INPUTMUX_Init((void *)INPUTMUX_BASE);
	reg_write(INPUTMUX_BASE + ROUTE_OFFSET(DEFAULT_REF_COOKIE), 0x1FU);
	reg_write(INPUTMUX_BASE + ROUTE_OFFSET(DEFAULT_TAR_COOKIE), 0x1FU);
	INPUTMUX_Deinit((void *)INPUTMUX_BASE);
	freqme_image_read(clobbered);
}

void freqme_default_route(uint32_t *ref, uint32_t *tar)
{
	*ref = ROUTE_VALUE(DEFAULT_REF_COOKIE);
	*tar = ROUTE_VALUE(DEFAULT_TAR_COOKIE);
}

uint32_t freqme_alt_ref_cookie(void)
{
	return (uint32_t)COOKIE(ALT_REF_NODE);
}

uint32_t freqme_alt_ref_route(void)
{
	return ROUTE_VALUE(COOKIE(ALT_REF_NODE));
}

#define SOURCE_CLOCK_ON(node)                                                                      \
	COND_CODE_1(DT_NODE_HAS_PROP(node, clocks),                                                \
		    ((void)clock_control_on(DEVICE_DT_GET(DT_CLOCKS_CTLR(node)),               \
					    (clock_control_subsys_t)(uintptr_t)                \
						    DT_CLOCKS_CELL(node, name));),             \
		    ())

void freqme_enable_source_clocks(void)
{
	/* The driver queries these clocks and never turns them on; the consumer
	 * owns that. A clock that is always on refuses clock_control_on(), which
	 * is why the return value is not checked: the control measures the rate.
	 */
	DT_FOREACH_CHILD(REF_NODE, SOURCE_CLOCK_ON)
	DT_FOREACH_CHILD(TAR_NODE, SOURCE_CLOCK_ON)
}

static void measure_cb(const struct device *dev, const struct clock_monitor_event_data *evt,
		       void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if ((evt->events & (CLOCK_MONITOR_EVT_MEASURE_DONE | CLOCK_MONITOR_EVT_CLOCK_LOST)) !=
	    0U) {
		atomic_inc(&measure_events);
		k_sem_give(&measure_done);
	}
}

static void window_cb(const struct device *dev, const struct clock_monitor_event_data *evt,
		      void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if ((evt->events & (CLOCK_MONITOR_EVT_FREQ_HIGH | CLOCK_MONITOR_EVT_FREQ_LOW)) != 0U) {
		atomic_inc(&window_events);
	}
}

int freqme_configure_measure(uint32_t window_ns)
{
	struct clock_monitor_config cfg = {
		.mode = CLOCK_MONITOR_MODE_MEASURE,
		.measure = {.window_ns = window_ns},
		.callback = measure_cb,
	};

	return clock_monitor_configure(freqme_dev(), &cfg);
}

int freqme_measure_start(void)
{
	k_sem_reset(&measure_done);

	return clock_monitor_start(freqme_dev());
}

/*
 * Busy-waited rather than slept: a sleep lets the idle thread enter a power
 * state in the layers that have one, and the sweep then suspends the device,
 * which aborts the measurement being waited for.
 */
int freqme_measure_wait(uint32_t timeout_ms)
{
	int64_t end = k_uptime_get() + timeout_ms;

	while (k_sem_take(&measure_done, K_NO_WAIT) != 0) {
		if (k_uptime_get() >= end) {
			(void)clock_monitor_stop(freqme_dev());
			return -ETIMEDOUT;
		}
		k_busy_wait(100);
	}

	return 0;
}

int freqme_start_and_wait(uint32_t timeout_ms)
{
	int err;

	err = freqme_measure_start();
	if (err != 0) {
		return err;
	}

	return freqme_measure_wait(timeout_ms);
}

int freqme_measure_once(uint32_t window_ns, uint32_t *hz)
{
	int err;

	err = freqme_configure_measure(window_ns);
	if (err != 0) {
		return err;
	}
	err = freqme_start_and_wait(100U + window_ns / 1000000U * 2U);
	if (err != 0) {
		return err;
	}

	return clock_monitor_get_rate(freqme_dev(), hz);
}

bool freqme_rate_plausible(uint32_t hz)
{
	uint32_t slack = FREQME_TARGET_HZ / 100U * FREQME_TOLERANCE_PCT;

	return (hz >= FREQME_TARGET_HZ - slack) && (hz <= FREQME_TARGET_HZ + slack);
}

int freqme_start_window(bool faulty)
{
	struct clock_monitor_config cfg = {
		.mode = CLOCK_MONITOR_MODE_WINDOW,
		.window =
			{
				.expected_hz = faulty ? 2U * FREQME_TARGET_HZ : FREQME_TARGET_HZ,
				.tolerance_ppm = faulty ? 10000U : FREQME_TOLERANCE_PCT * 10000U,
				.window_ns = FREQME_WINDOW_NS,
			},
		.callback = window_cb,
	};
	int err;

	err = clock_monitor_configure(freqme_dev(), &cfg);
	if (err != 0) {
		return err;
	}

	return clock_monitor_start(freqme_dev());
}

uint32_t freqme_window_events(void)
{
	return (uint32_t)atomic_get(&window_events);
}

void freqme_window_events_reset(void)
{
	(void)atomic_set(&window_events, 0);
}

bool freqme_window_events_arrive(uint32_t timeout_ms)
{
	int64_t end = k_uptime_get() + timeout_ms;

	/* Busy-waited for the same reason as freqme_measure_wait(). */
	freqme_window_events_reset();
	while (k_uptime_get() < end) {
		if (freqme_window_events() != 0U) {
			return true;
		}
		k_busy_wait(100);
	}

	return false;
}

void freqme_report_configuration(void)
{
	uint32_t ref;
	uint32_t tar;

	freqme_default_route(&ref, &tar);
	TC_PRINT("freqme0 @ 0x%08lx, inputmux @ 0x%08lx\n", (unsigned long)freqme_base,
		 (unsigned long)INPUTMUX_BASE);
	TC_PRINT("default route: REF 0x%03x <- %u, TAR 0x%03x <- %u; alternate REF <- %u\n",
		 ROUTE_OFFSET(DEFAULT_REF_COOKIE), ref, ROUTE_OFFSET(DEFAULT_TAR_COOKIE), tar,
		 freqme_alt_ref_route());
}

void freqme_report_hw(const char *when)
{
	struct freqme_image img;
	uint32_t raw = ctrlstat();

	freqme_image_read(&img);
	TC_PRINT("freqme %s: CTRLSTAT=0x%08x (measuring=%u continuous=%u ie=0x%x) MIN=0x%08x "
		 "MAX=0x%08x route REF=%u TAR=%u\n",
		 when, raw, (unsigned int)((raw & CTRLSTAT_IN_PROGRESS) != 0U),
		 (unsigned int)((raw & CTRLSTAT_CONTINUOUS) != 0U),
		 (unsigned int)(raw & CTRLSTAT_INT_ENABLES), img.min, img.max, img.route_ref,
		 img.route_tar);
}
