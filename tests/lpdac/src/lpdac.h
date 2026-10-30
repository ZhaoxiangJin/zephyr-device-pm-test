/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief What every layer of the LPDAC case needs from the hardware.
 *
 * A DAC output has no completion event, so there is no operation whose success
 * could stand in for "the block is powered and configured" the way a conversion
 * does next door. What this case observes instead is GCR, the only register the
 * driver's PM callbacks write. DACEN and the optional BUF_EN are output
 * enables; SUSPEND clears them and RESUME puts them back. The remaining
 * fields are the configuration, which TURN_ON rebuilds.
 *
 * Two things are deliberately not observed. DATA is write-only -- MCX Nx4x RM
 * Rev. 6_RC2 section 42.7.1.4 shows a zero read row -- so the code the driver
 * restores cannot be read back, and the output level itself needs a meter rather
 * than a test. Every claim below is therefore about the buffer being on and the
 * block being configured, not about the voltage.
 */

#ifndef LPDAC_PM_H_
#define LPDAC_PM_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#define LPDAC_USER_NODE DT_PATH(zephyr_user)

#if !DT_NODE_HAS_PROP(LPDAC_USER_NODE, dac) ||                                                     \
	!DT_NODE_HAS_PROP(LPDAC_USER_NODE, dac_channel_id) ||                                      \
	!DT_NODE_HAS_PROP(LPDAC_USER_NODE, dac_resolution)
#error "This board has no DAC wiring: add boards/<board_target>.overlay with a zephyr,user node"
#endif

#define LPDAC_NODE DT_PHANDLE(LPDAC_USER_NODE, dac)

#if !DT_NODE_HAS_COMPAT(LPDAC_NODE, nxp_lpdac)
#error "zephyr,user dac must name an nxp,lpdac node; nxp,hpdac is a different driver"
#endif

#if !defined(CONFIG_DAC_MCUX_LPDAC)
#error "CONFIG_DAC_MCUX_LPDAC is not enabled, so there is no driver to test"
#endif

/** The DAC under test. */
#define LPDAC_DEV DEVICE_DT_GET(LPDAC_NODE)

/** The channel and resolution the board's zephyr,user node names. */
#define LPDAC_CHANNEL_ID DT_PROP(LPDAC_USER_NODE, dac_channel_id)
#define LPDAC_RESOLUTION DT_PROP(LPDAC_USER_NODE, dac_resolution)

/** Mid-scale: in range on every resolution, and not the reset value. */
#define LPDAC_MIDSCALE (BIT(LPDAC_RESOLUTION) / 2U)

/**
 * @brief Configure channel @ref LPDAC_CHANNEL_ID.
 *
 * Also exercised as a claim in its own right by the layers that call it against a
 * device that is not active, where it has to be refused.
 *
 * @return 0 on success, negative errno from dac_channel_setup() otherwise.
 */
int lpdac_setup(void);

/**
 * @brief Drive @p value out of that channel.
 *
 * @return 0 on success, negative errno from dac_write_value() otherwise.
 */
int lpdac_write(uint32_t value);

#if defined(CONFIG_PM_TEST_LAYER_RUNTIME)
/** @brief Capture the runtime boot state before the control configures the DAC. */
void lpdac_capture_runtime_boot(void);
#endif

/** @brief GCR in full, for a log line or an assertion message. */
uint32_t lpdac_gcr(void);

/** @brief GCR configuration, excluding output enables. */
uint32_t lpdac_gcr_config(void);

/** @brief True while every implemented output enable is set. */
bool lpdac_output_on(void);

/** @brief True while every implemented output enable is clear. */
bool lpdac_output_off(void);

/**
 * @brief Clear GCR, leaving the block looking the way a reset leaves it.
 *
 * What stands in for a power cycle in a layer where nothing really cut power to
 * the block, so that TURN_ON has something to restore.
 */
void lpdac_gcr_clobber(void);

/** @brief Print what the devicetree asked for, once, from the suite setup. */
void lpdac_report_configuration(void);

/**
 * @brief Print the register view.
 *
 * @param when short label for where in the sequence this is, e.g. "before".
 */
void lpdac_report_hw(const char *when);

/** @brief Print GCR captured before reporting changes the hardware state. */
void lpdac_report_hw_snapshot(const char *when, uint32_t gcr);

#endif /* LPDAC_PM_H_ */
