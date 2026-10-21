/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief What every layer of the WWDT case needs from the hardware.
 *
 * The case uses one watchdog session per image, in interrupt-only mode
 * (WDT_FLAG_RESET_NONE, MOD[WDRESET] clear). Idle checks precede setup,
 * running checks share that session, and a separate terminal suite disables
 * the watchdog after the running suite completes.
 *
 * Four things are observed.
 *
 * The WWDT bus clock gate, read out of the clock controller: a WWDT register
 * only answers while the block is clocked, and whether it is clocked is the
 * question. Releasing that clock is the whole of what SUSPEND does to an idle
 * watchdog.
 *
 * The configuration image -- the enable and mode bits of MOD, TC, WINDOW and
 * WARNINT. Idle Deep Power Down clears a TC sentinel to its reset value, 0xFF,
 * without enabling the watchdog. A subsequent setup uses the retained RAM
 * request.
 *
 * TV, the counter. A watchdog that counts down is what makes "MOD[WDEN] is set"
 * mean a watchdog that runs, and a TV that jumps back up is what makes a feed a
 * feed.
 *
 * The timeout callback, which must never fire: it would mean the case let the
 * watchdog run out.
 */

#ifndef WWDT_PM_H_
#define WWDT_PM_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#define WWDT_NODE DT_NODELABEL(wwdt0)

#if !DT_NODE_HAS_STATUS_OKAY(WWDT_NODE)
#error "wwdt0 is not enabled on this board"
#endif

#if !defined(CONFIG_WDT_MCUX_WWDT)
#error "CONFIG_WDT_MCUX_WWDT is not enabled, so there is no driver to test"
#endif

/** Timeout the case installs. The WWDT divides its clock by 4 into a 24-bit
 * counter, so this fits for any WWDT clock up to 6.7 MHz, and it is far longer
 * than any gap between two feeds here.
 */
#define WWDT_TIMEOUT_MS 10000U

extern const struct device *const wwdt_dev;

/** @brief True while the WWDT bus clock gate is open. */
bool wwdt_gate_is_open(void);

/** @brief True while the functional divider is halted. */
bool wwdt_clock_is_halted(void);

/** The configuration a power-down resets. Only readable with the gate open. */
struct wwdt_image {
  /** MOD with the status flags WDTOF and WDINT masked out. */
  uint32_t mod;
  uint32_t tc;
  uint32_t window;
  uint32_t warnint;
};

/** @brief Sample @ref wwdt_image. */
void wwdt_image_read(struct wwdt_image *img);

/** @brief True when two images are the same. */
bool wwdt_image_equal(const struct wwdt_image *a, const struct wwdt_image *b);

/** @brief True when MOD[WDEN] is set in @p img. */
bool wwdt_image_enabled(const struct wwdt_image *img);

/** @brief True when MOD[WDRESET] is set in @p img. */
bool wwdt_image_resets(const struct wwdt_image *img);

/** @brief The counter, TV. Only readable with the gate open. */
uint32_t wwdt_tv(void);

/** @brief True when two reads of TV a short wait apart show it counting down.
 */
bool wwdt_counting(void);

/**
 * @brief Let the counter run down a little, feed through wdt_feed(), and check
 *        that TV went back up.
 *
 * @return 0, the wdt_feed() error, or -ESTALE when TV did not reload.
 */
int wwdt_feed_and_verify(void);

/** @brief True once the timeout callback has run. */
bool wwdt_expired(void);

/**
 * @brief wdt_install_timeout(), once per image.
 *
 * A layer that wants to call wdt_setup() before the watchdog runs installs the
 * timeout itself; the start in the suite setup then reuses it.
 *
 * @return 0, or the wdt_install_timeout() error.
 */
int wwdt_install(void);

/**
 * @brief Hook a layer implements to observe the device before the watchdog
 * runs.
 *
 * Called from suite setup before the watchdog starts. Return an error if the
 * pre-start sequence cannot complete; otherwise record observations for the
 * test.
 */
int wwdt_layer_before_start(void);

/** @brief Check the layer's transition after successful terminal disable. */
void wwdt_layer_after_stop(void);

/** @brief Result of starting the watchdog from the suite setup. */
extern int wwdt_start_rc;

/** @brief Install and wdt_setup(), from the suite setup. */
int wwdt_start(void);

/** @brief Set a non-reset TC value while WDEN is clear and the bus gate is
 * open. */
int wwdt_seed_idle_registers(void);

/** @brief Print what the devicetree asked for, once, from the suite setup. */
void wwdt_report_configuration(void);

/**
 * @brief Print the gate and, when the gate is open, the image and TV.
 *
 * Safe to call with the gate closed: it says the registers were not read rather
 * than touching an unclocked block.
 */
void wwdt_report_hw(const char *when);

#endif /* WWDT_PM_H_ */
