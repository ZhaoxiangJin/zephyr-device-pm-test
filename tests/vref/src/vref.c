/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The register view of the VREF block, shared by every layer.
 */

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/dt-bindings/regulator/nxp_vref.h>
#include <zephyr/tc_util.h>

#include <fsl_device_registers.h>

#include "vref.h"

static VREF_Type *const base = (VREF_Type *)DT_REG_ADDR(VREF_NODE);

/*
 * The MCXN families do not define FSL_FEATURE_VREF_HAS_TRIM2V1 at all, so both of
 * them take the second branch, the same way the driver does.
 */
#if defined(FSL_FEATURE_VREF_HAS_TRIM2V1) && (FSL_FEATURE_VREF_HAS_TRIM2V1 == 0)
#define VREF_TRIM_MASK  VREF_UTRIM_VREFTRIM_MASK
#define VREF_TRIM_SHIFT VREF_UTRIM_VREFTRIM_SHIFT
const bool vref_trim_is_factory = true;
#else
#define VREF_TRIM_MASK  VREF_UTRIM_TRIM2V1_MASK
#define VREF_TRIM_SHIFT VREF_UTRIM_TRIM2V1_SHIFT
const bool vref_trim_is_factory = false;
#endif

/* The bits TURN_ON is expected to put back, taken from the same devicetree
 * properties the driver reads.
 */
#define VREF_DT_CSR_BITS                                                                           \
	((DT_PROP(VREF_NODE, nxp_current_compensation_en) ? VREF_CSR_ICOMPEN_MASK : 0U) |          \
	 (DT_PROP(VREF_NODE, nxp_chop_oscillator_en) ? VREF_CSR_CHOPEN_MASK : 0U) |                \
	 (DT_PROP(VREF_NODE, nxp_internal_voltage_regulator_en) ? VREF_CSR_REGEN_MASK : 0U))

/*
 * regulator-initial-mode is applied by regulator_common_init() at boot. When the
 * node carries none, nothing calls set_mode() and the block is left in the state
 * configure_hw() leaves it in, which get_mode() reads back as STANDBY.
 */
#define VREF_DT_MODE DT_PROP_OR(VREF_NODE, regulator_initial_mode, NXP_VREF_MODE_STANDBY)

/* In range, and distinct from both the bottom of the range and the code
 * vref_pick_voltage() settles on, so a restore can be told from "nothing happened".
 */
#define VREF_TRIM_CLOBBER 0x3U

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

uint32_t vref_csr(void)
{
	return base->CSR;
}

uint32_t vref_trim(void)
{
	return (base->UTRIM & VREF_TRIM_MASK) >> VREF_TRIM_SHIFT;
}

bool vref_config_bits_present(void)
{
	return (base->CSR & VREF_DT_CSR_BITS) == VREF_DT_CSR_BITS;
}

void vref_config_bits_clobber(void)
{
	base->CSR &= ~VREF_DT_CSR_BITS;
}

bool vref_mode_is_configured(void)
{
	regulator_mode_t mode;

	return (regulator_get_mode(VREF_DEV, &mode) == 0) && (mode == VREF_DT_MODE);
}

bool vref_output_stable(void)
{
	return (base->CSR & VREF_CSR_VREFST_MASK) != 0U;
}

uint32_t vref_trim_clobber(void)
{
	base->UTRIM = (base->UTRIM & ~VREF_TRIM_MASK) |
		      ((VREF_TRIM_CLOBBER << VREF_TRIM_SHIFT) & VREF_TRIM_MASK);

	return VREF_TRIM_CLOBBER;
}

int vref_pick_voltage(int32_t *volt_uv)
{
	unsigned int count = regulator_count_voltages(VREF_DEV);

	if (count == 0U) {
		return -ENOTSUP;
	}

	return regulator_list_voltage(VREF_DEV, count / 2U, volt_uv);
}

void vref_report_configuration(void)
{
	TC_PRINT("%s at 0x%08x: devicetree CSR bits 0x%08x, initial mode %s, trim variant %s\n",
		 VREF_DEV->name, (unsigned int)DT_REG_ADDR(VREF_NODE),
		 (unsigned int)VREF_DT_CSR_BITS, mode_str(VREF_DT_MODE),
		 vref_trim_is_factory ? "VREFTRIM (factory)" : "TRIM2V1");
}

void vref_report_hw(const char *when)
{
	regulator_mode_t mode = NXP_VREF_MODE_STANDBY;
	int32_t volt_uv = 0;

	(void)regulator_get_mode(VREF_DEV, &mode);
	(void)regulator_get_voltage(VREF_DEV, &volt_uv);

	TC_PRINT("hw(%s) CSR=0x%08x trim=0x%x mode=%s %d uV VREFST=%u enabled=%u\n", when,
		 base->CSR, vref_trim(), mode_str(mode), volt_uv, vref_output_stable() ? 1U : 0U,
		 regulator_is_enabled(VREF_DEV) ? 1U : 0U);
}
