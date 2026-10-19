/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/dt-bindings/regulator/nxp_vref.h>
#include <zephyr/tc_util.h>

#include <fsl_device_registers.h>

#include "vref.h"

static VREF_Type *const base = (VREF_Type *)DT_REG_ADDR(VREF_NODE);

#if defined(FSL_FEATURE_VREF_HAS_TRIM2V1) && (FSL_FEATURE_VREF_HAS_TRIM2V1 == 0)
#define VREF_TRIM_MASK  VREF_UTRIM_VREFTRIM_MASK
#define VREF_TRIM_SHIFT VREF_UTRIM_VREFTRIM_SHIFT
const bool vref_trim_is_factory = true;
#else
#define VREF_TRIM_MASK  VREF_UTRIM_TRIM2V1_MASK
#define VREF_TRIM_SHIFT VREF_UTRIM_TRIM2V1_SHIFT
const bool vref_trim_is_factory = false;
#endif

#define VREF_DT_CSR_BITS                                                                           \
	((DT_PROP(VREF_NODE, nxp_current_compensation_en) ? VREF_CSR_ICOMPEN_MASK : 0U) |          \
	 (DT_PROP(VREF_NODE, nxp_chop_oscillator_en) ? VREF_CSR_CHOPEN_MASK : 0U) |                \
	 (DT_PROP(VREF_NODE, nxp_internal_voltage_regulator_en) ? VREF_CSR_REGEN_MASK : 0U))

#define VREF_DT_MODE DT_PROP_OR(VREF_NODE, regulator_initial_mode, NXP_VREF_MODE_STANDBY)

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
	return (base->CSR & (VREF_CSR_ICOMPEN_MASK | VREF_CSR_CHOPEN_MASK |
			     VREF_CSR_REGEN_MASK)) == VREF_DT_CSR_BITS;
}

void vref_config_bits_clobber(void)
{
	base->CSR ^= VREF_CSR_ICOMPEN_MASK | VREF_CSR_CHOPEN_MASK | VREF_CSR_REGEN_MASK;
}

regulator_mode_t vref_hw_mode(void)
{
	uint32_t csr = base->CSR;

	if ((csr & VREF_CSR_BUF21EN_MASK) == 0U) {
		return NXP_VREF_MODE_STANDBY;
	}
	return (csr & VREF_CSR_HI_PWR_LV_MASK) != 0U ?
		NXP_VREF_MODE_HIGH_POWER : NXP_VREF_MODE_LOW_POWER;
}

bool vref_mode_is_configured(void)
{
	return vref_hw_mode() == VREF_DT_MODE;
}

bool vref_output_disabled(void)
{
	uint32_t mask = VREF_CSR_HCBGEN_MASK | VREF_CSR_BUF21EN_MASK;

#if defined(FSL_FEATURE_VREF_HAS_LPBGEN) && (FSL_FEATURE_VREF_HAS_LPBGEN == 0)
	mask |= VREF_CSR_IBIAS_EN_MASK;
#else
	mask |= VREF_CSR_LPBGEN_MASK;
#endif
#if !(defined(FSL_FEATURE_VREF_HAS_LOWPOWER_BUFFER) && (FSL_FEATURE_VREF_HAS_LOWPOWER_BUFFER == 0))
	mask |= VREF_CSR_LPBG_BUF_EN_MASK;
#endif
	return (base->CSR & mask) == 0U;
}

bool vref_output_stable(void)
{
	uint32_t mask = VREF_CSR_HCBGEN_MASK | VREF_CSR_VREFST_MASK;

	return (base->CSR & mask) == mask;
}

uint32_t vref_trim_clobber(void)
{
	uint32_t code = vref_trim() == 3U ? 0U : 3U;

	base->UTRIM = (base->UTRIM & ~VREF_TRIM_MASK) |
		      ((code << VREF_TRIM_SHIFT) & VREF_TRIM_MASK);

	return code;
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
	TC_PRINT("hw(%s) CSR=0x%08x trim=0x%x mode=%s stable=%u enabled=%u\n", when,
		 base->CSR, vref_trim(), mode_str(vref_hw_mode()), vref_output_stable() ? 1U : 0U,
		 regulator_is_enabled(VREF_DEV) ? 1U : 0U);
}
