/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The register view of the LPDAC block, shared by every layer.
 */

#include <zephyr/drivers/dac.h>
#include <zephyr/sys/util.h>
#include <zephyr/tc_util.h>

#include <fsl_device_registers.h>

#include "lpdac.h"

#if defined(FSL_FEATURE_LPDAC_HAS_GCR_BUF_EN) && \
	FSL_FEATURE_LPDAC_HAS_GCR_BUF_EN
#define LPDAC_OUTPUT_ENABLE_MASK (LPDAC_GCR_DACEN_MASK | LPDAC_GCR_BUF_EN_MASK)
#else
#define LPDAC_OUTPUT_ENABLE_MASK LPDAC_GCR_DACEN_MASK
#endif

static LPDAC_Type *const base = (LPDAC_Type *)DT_REG_ADDR(LPDAC_NODE);

static const struct dac_channel_cfg lpdac_ch_cfg = {
	.channel_id = LPDAC_CHANNEL_ID,
	.resolution = LPDAC_RESOLUTION,
};

int lpdac_setup(void)
{
	return dac_channel_setup(LPDAC_DEV, &lpdac_ch_cfg);
}

int lpdac_write(uint32_t value)
{
	return dac_write_value(LPDAC_DEV, LPDAC_CHANNEL_ID, value);
}

uint32_t lpdac_gcr(void)
{
	return base->GCR;
}

uint32_t lpdac_gcr_config(void)
{
	return base->GCR & ~LPDAC_OUTPUT_ENABLE_MASK;
}

bool lpdac_output_on(void)
{
	uint32_t enables = base->GCR & LPDAC_OUTPUT_ENABLE_MASK;

	return enables == LPDAC_OUTPUT_ENABLE_MASK;
}

bool lpdac_output_off(void)
{
	return (base->GCR & LPDAC_OUTPUT_ENABLE_MASK) == 0U;
}

void lpdac_gcr_clobber(void)
{
	base->GCR = 0U;
}

void lpdac_report_configuration(void)
{
	TC_PRINT("%s at 0x%08x: channel %u, %u-bit, mid-scale code %u\n", LPDAC_DEV->name,
		 (unsigned int)DT_REG_ADDR(LPDAC_NODE), (unsigned int)LPDAC_CHANNEL_ID,
		 (unsigned int)LPDAC_RESOLUTION, (unsigned int)LPDAC_MIDSCALE);
}

void lpdac_report_hw(const char *when)
{
	lpdac_report_hw_snapshot(when, lpdac_gcr());
}

void lpdac_report_hw_snapshot(const char *when, uint32_t gcr)
{
	TC_PRINT("hw(%s) GCR=0x%08x DACEN=%u\n", when, gcr,
		 (gcr & LPDAC_GCR_DACEN_MASK) != 0U ? 1U : 0U);
}
