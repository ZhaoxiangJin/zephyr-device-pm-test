/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The MRT wiring, and the hardware truths this case asserts on.
 */

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/tc_util.h>

#include <zephyr/dt-bindings/clock/mcux_lpc_syscon_clock.h>
#include <fsl_clock.h>
#include <soc.h>

#include "mrt.h"

/*
 * The module takes its clock from a syscon node with a single MCUX_MRT_CLK cell,
 * which is also what names the clock gate below. A part wiring this driver to some
 * other clock controller would need a different mapping and is refused here rather
 * than read out of the wrong register.
 */
#define MRT_ASSERT_SYSCON_CLOCK(node_id)                                                           \
	BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CLOCKS_CTLR(node_id), nxp_lpc_syscon),                  \
		     "this case reads the MRT clock gate out of the MCX clock controller and "      \
		     "only fits SoCs whose MRT clock comes from nxp,lpc-syscon");

DT_FOREACH_STATUS_OKAY(nxp_mrt, MRT_ASSERT_SYSCON_CLOCK)

/*
 * One MRT module per build. Every enabled channel below belongs to it, which is why
 * the gate accessors take no instance: the four channels share one gate.
 */
#define MRT_PM_COUNT_MODULE(node_id) +1
BUILD_ASSERT((0 DT_FOREACH_STATUS_OKAY(nxp_mrt, MRT_PM_COUNT_MODULE)) == 1,
	     "this case assumes a single enabled nxp,mrt module");

#define MRT_ENTRY(node_id)                                                                         \
	{                                                                                          \
		.dev = DEVICE_DT_GET(node_id),                                                     \
		.module = DEVICE_DT_GET(DT_PARENT(node_id)),                                       \
		.channel = (uint8_t)DT_REG_ADDR(node_id),                                          \
		.base = (uintptr_t)DT_REG_ADDR(DT_PARENT(node_id)),                                \
	},

const struct mrt_case mrt_cases[MRT_CASE_COUNT] = {
	DT_FOREACH_STATUS_OKAY(nxp_mrt_channel, MRT_ENTRY)
};

/* Counted from the top callback; one slot per channel, indexed by mrt_cases. */
static volatile uint32_t mrt_tick_count[MRT_CASE_COUNT];

static MRT_Type *mrt_base(const struct mrt_case *c)
{
	return (MRT_Type *)c->base;
}

static uint8_t mrt_slot(const struct mrt_case *c)
{
	return (uint8_t)(c - &mrt_cases[0]);
}

static bool mrt_gate_of(clock_ip_name_t *gate)
{
#if defined(CONFIG_SOC_FAMILY_LPC) || defined(CONFIG_SOC_SERIES_RW6XX) ||                          \
	defined(CONFIG_SOC_FAMILY_MCXN) || defined(CONFIG_SOC_SERIES_MCXW2XX)
	*gate = kCLOCK_Mrt;
#elif defined(CONFIG_SOC_FAMILY_NXP_IMXRT)
	*gate = kCLOCK_Mrt0;
#else
	return false;
#endif

	return *gate != kCLOCK_None;
}

bool mrt_gate_observable(void)
{
	clock_ip_name_t gate;

	return mrt_gate_of(&gate);
}

/*
 * CLOCK_EnableClock()/CLOCK_DisableClock() write through the write-only SET/CLR
 * aliases, so the readable register is the one at the head of the group:
 * SYSCON->AHBCLKCTRL0..3, contiguous from 0x200, which is what the HAL's
 * CLK_GATE_ABSTRACT_REG_OFFSET() encodes.
 */
bool mrt_gate_is_open(void)
{
	clock_ip_name_t gate;

	if (!mrt_gate_of(&gate)) {
		return false;
	}

	const volatile uint32_t *reg = &(&SYSCON->AHBCLKCTRL0)[CLK_GATE_ABSTRACT_REG_OFFSET(gate)];

	return (*reg & BIT(CLK_GATE_ABSTRACT_BITS_SHIFT(gate))) != 0U;
}

void mrt_gate_open(void)
{
	clock_ip_name_t gate;

	if (mrt_gate_of(&gate)) {
		CLOCK_EnableClock(gate);
	}
}

void mrt_gate_close(void)
{
	clock_ip_name_t gate;

	if (mrt_gate_of(&gate)) {
		CLOCK_DisableClock(gate);
	}
}

void mrt_image_read(const struct mrt_case *c, struct mrt_image *img)
{
	MRT_Type *base = mrt_base(c);

	img->ctrl = base->CHANNEL[c->channel].CTRL;
	img->intval = base->CHANNEL[c->channel].INTVAL & MRT_CHANNEL_INTVAL_IVALUE_MASK;
	img->running = (base->CHANNEL[c->channel].STAT & MRT_CHANNEL_STAT_RUN_MASK) != 0U;
}

bool mrt_image_equal(const struct mrt_image *a, const struct mrt_image *b)
{
	return (a->ctrl == b->ctrl) && (a->intval == b->intval) && (a->running == b->running);
}

void mrt_clobber(const struct mrt_case *c, struct mrt_image *clobbered)
{
	MRT_Type *base = mrt_base(c);
	bool was_open = mrt_gate_is_open();

	if (!was_open) {
		mrt_gate_open();
	}

	base->CHANNEL[c->channel].CTRL = 0U;
	mrt_image_read(c, clobbered);

	if (!was_open) {
		mrt_gate_close();
	}
}

bool mrt_running(const struct mrt_case *c)
{
	MRT_Type *base = mrt_base(c);

	return (base->CHANNEL[c->channel].STAT & MRT_CHANNEL_STAT_RUN_MASK) != 0U;
}

static void mrt_top_handler(const struct device *dev, void *user_data)
{
	ARG_UNUSED(dev);

	mrt_tick_count[(uintptr_t)user_data]++;
}

int mrt_start(const struct mrt_case *c)
{
	struct counter_top_cfg top = {
		.ticks = counter_us_to_ticks(c->dev, MRT_PERIOD_US),
		.callback = mrt_top_handler,
		.user_data = (void *)(uintptr_t)mrt_slot(c),
		.flags = 0U,
	};
	int err;

	mrt_tick_count[mrt_slot(c)] = 0U;

	err = counter_set_top_value(c->dev, &top);
	if (err != 0) {
		return err;
	}

	return counter_start(c->dev);
}

int mrt_stop(const struct mrt_case *c)
{
	return counter_stop(c->dev);
}

uint32_t mrt_ticks(const struct mrt_case *c)
{
	return mrt_tick_count[mrt_slot(c)];
}

bool mrt_wait_ticks(const struct mrt_case *c, uint32_t want, uint32_t timeout_ms)
{
	uint32_t start = mrt_ticks(c);

	for (uint32_t waited = 0U; waited < timeout_ms; waited++) {
		if ((mrt_ticks(c) - start) >= want) {
			return true;
		}
		k_msleep(1);
	}

	return (mrt_ticks(c) - start) >= want;
}

bool mrt_advances(const struct mrt_case *c)
{
	MRT_Type *base = mrt_base(c);
	uint32_t first = base->CHANNEL[c->channel].TIMER & MRT_CHANNEL_TIMER_VALUE_MASK;

	/*
	 * The channel counts down and reloads, so two samples can coincide by chance.
	 * A whole period apart they cannot both be the reload value unless the counter
	 * is standing still.
	 */
	k_busy_wait(MRT_PERIOD_US / 4U);

	return (base->CHANNEL[c->channel].TIMER & MRT_CHANNEL_TIMER_VALUE_MASK) != first;
}

void mrt_report_configuration(void)
{
	TC_PRINT("MRT: %d channel(s) under test, period %u us\n", MRT_CASE_COUNT, MRT_PERIOD_US);

	if (!mrt_gate_observable()) {
		TC_PRINT("MRT: this SoC's HAL names no MRT clock gate\n");
	}

	MRT_FOREACH_CASE(c) {
		TC_PRINT("MRT: %s is channel %u of %s, period %u ticks at %u Hz\n", c->dev->name,
			 c->channel, c->module->name, counter_us_to_ticks(c->dev, MRT_PERIOD_US),
			 counter_get_frequency(c->dev));
	}
}

void mrt_report_hw(const struct mrt_case *c, const char *when)
{
	if (!mrt_gate_is_open()) {
		TC_PRINT("%s %s: gate closed, registers not read\n", c->dev->name, when);
		return;
	}

	struct mrt_image img;

	mrt_image_read(c, &img);
	TC_PRINT("%s %s: gate open, CTRL=0x%08x INTVAL=%u RUN=%d, %u tick(s)\n", c->dev->name,
		 when, img.ctrl, img.intval, (int)img.running, mrt_ticks(c));
}
