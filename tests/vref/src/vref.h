/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief What every layer of the VREF case needs from the hardware.
 *
 * Unlike the converter next door, a voltage reference has no operation to
 * perform: what the driver's PM callback restores is register content that is
 * written once and then only lost to a reset of the block. So the observables here
 * are the registers themselves -- the three CSR configuration bits the devicetree
 * asks for, the UTRIM trim code, the mode, and the CSR status bit that says the
 * output has settled. The register block is always clocked (the MCXN vref node
 * carries no clocks property, so the driver's clock branch is skipped), which is
 * what makes reading them safe in every layer.
 */

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

/** The reference under test. */
#define VREF_DEV DEVICE_DT_GET(VREF_NODE)

/**
 * @brief True on the variants whose trim register holds a factory value.
 *
 * Those parts expose only UTRIM[VREFTRIM], loaded at reset, and the driver is
 * right to leave it alone when no consumer has asked for a voltage. Everywhere
 * else the trim is UTRIM[TRIM2V1] and the driver's untrimmed restore path drives
 * it to the bottom of the range. A `const bool` rather than a macro so the tests
 * can branch on it without preprocessor conditionals.
 */
extern const bool vref_trim_is_factory;

/** @brief CSR, for a log line or an assertion message. */
uint32_t vref_csr(void);

/** @brief The trim code, whichever field this variant keeps it in. */
uint32_t vref_trim(void);

/**
 * @brief True when CSR carries every configuration bit the devicetree asks for.
 *
 * ICOMPEN, CHOPEN and REGEN, each included only if the node asks for it, so the
 * expectation follows the board rather than restating one board's configuration.
 * The driver writes them from configure_hw(), which runs on TURN_ON.
 */
bool vref_config_bits_present(void);

/**
 * @brief Clear every configuration bit the devicetree asks for.
 *
 * The other half of @ref vref_trim_clobber: together they leave the block looking
 * the way it looks after a reset, in a layer where nothing really cut power to it.
 */
void vref_config_bits_clobber(void);

/** @brief True when get_mode() reads back the node's regulator-initial-mode. */
bool vref_mode_is_configured(void);

/** @brief True while CSR[VREFST] says the output has settled. */
bool vref_output_stable(void);

/**
 * @brief Write an in-range trim code that is neither the bottom of the range nor
 *        the one @ref vref_pick_voltage settles on.
 *
 * This is what stands in for a reset of the block in a layer where nothing really
 * cut power to it. Writing UTRIM directly rather than going through
 * regulator_set_voltage() is deliberate: set_voltage() would latch the driver's
 * trim_set and there would be no untrimmed restore branch left to observe.
 *
 * @return the code it wrote, so a later reading of the trim can be compared
 *         against it.
 */
uint32_t vref_trim_clobber(void);

/**
 * @brief Pick a voltage out of the middle of whatever range this variant exposes.
 *
 * Chosen the way a consumer would choose it, rather than hard-coded to one part's
 * register layout.
 *
 * @param volt_uv where to store the voltage; untouched on failure.
 *
 * @return 0 on success, negative errno otherwise.
 */
int vref_pick_voltage(int32_t *volt_uv);

/** @brief Print what the devicetree asked for, once, from the suite setup. */
void vref_report_configuration(void);

/**
 * @brief Print the register view.
 *
 * @param when short label for where in the sequence this is, e.g. "before".
 */
void vref_report_hw(const char *when);

#endif /* VREF_PM_H_ */
