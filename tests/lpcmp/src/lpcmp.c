/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief LPCMP plumbing shared by every layer of this case.
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/comparator.h>
#include <zephyr/kernel.h>

#include <soc.h> /* LPCMP_Type, LPCMP_CCR0_CMP_EN_MASK */

#include "lpcmp.h"

#if !DT_NODE_EXISTS(DT_ALIAS(test_comp))
#error "This board has no test-comp alias; see boards/<board_target>.overlay"
#endif

#define LPCMP_NODE DT_ALIAS(test_comp)

const struct device *const lpcmp_dev = DEVICE_DT_GET(LPCMP_NODE);

static const LPCMP_Type *lpcmp_regs(void)
{
	return (const LPCMP_Type *)DT_REG_ADDR(LPCMP_NODE);
}

bool lpcmp_cmp_en(void)
{
	return (lpcmp_regs()->CCR0 & LPCMP_CCR0_CMP_EN_MASK) != 0U;
}

uint32_t lpcmp_ccr2(void)
{
	return lpcmp_regs()->CCR2;
}

int lpcmp_output(void)
{
	return comparator_get_output(lpcmp_dev);
}

#if defined(CONFIG_PM_TEST_LPCMP_LOOPBACK)

#include <zephyr/drivers/gpio.h>

#if !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), test_gpios)
#error "The loopback layer needs a zephyr,user test-gpios property"
#endif

static const struct gpio_dt_spec loopback_gpio =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), test_gpios);

/* Long enough for the comparator's filter and the pin itself to settle; a
 * comparator that is comparing at all follows within a millisecond or two.
 */
#define LOOPBACK_SETTLE_MS 50

static bool output_follows(int level)
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

int lpcmp_loopback_init(void)
{
	if (!gpio_is_ready_dt(&loopback_gpio)) {
		return -ENODEV;
	}
	return gpio_pin_configure_dt(&loopback_gpio, GPIO_OUTPUT_INACTIVE);
}

bool lpcmp_loopback_tracks_input(void)
{
	bool high_ok = output_follows(1);
	bool low_ok = output_follows(0);

	return high_ok && low_ok;
}
#endif /* CONFIG_PM_TEST_LPCMP_LOOPBACK */
