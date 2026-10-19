/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/* Register readback does not acquire runtime PM references. */

#ifndef VREF_PM_H_
#define VREF_PM_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/regulator.h>

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

/** True for the factory-loaded VREFTRIM-only variant. */
extern const bool vref_trim_is_factory;

uint32_t vref_csr(void);
uint32_t vref_trim(void);

/** Compare all configuration bits, including disabled DT options. */
bool vref_config_bits_present(void);
void vref_config_bits_clobber(void);

/** Compare the hardware mode with regulator-initial-mode. */
bool vref_mode_is_configured(void);

/** Decode CSR rather than the driver's cached mode. */
regulator_mode_t vref_hw_mode(void);

/** True when all bandgap, bias and buffer enable bits are clear. */
bool vref_output_disabled(void);

/** Require both HCBGEN and VREFST. */
bool vref_output_stable(void);

/** Corrupt UTRIM without changing the driver's saved trim; return the new code. */
uint32_t vref_trim_clobber(void);

int vref_pick_voltage(int32_t *volt_uv);

void vref_report_configuration(void);
void vref_report_hw(const char *when);

#endif /* VREF_PM_H_ */
