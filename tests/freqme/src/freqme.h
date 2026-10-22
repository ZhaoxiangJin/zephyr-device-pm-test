/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief What every layer of the FREQME case needs from the hardware.
 *
 * Three things are observed, all of them readable whether or not the block is
 * suspended: on these parts a FREQME register read with the peripheral gate
 * closed still returns the retained contents and does not fault.
 *
 * The control state, read back through CTRLSTAT. CTRL_W, which software writes,
 * is write-only, and CTRLSTAT mirrors every field of it: REF_SCALE, the mode
 * bits, the three interrupt enables, CONTINUOUS_MODE_EN and
 * MEASURE_IN_PROGRESS. Whether the block is measuring is the last two; whether
 * the consumer's configuration is in the block is the rest, together with MIN
 * and MAX.
 *
 * The INPUTMUX route that selects the reference and the target clock. It lives
 * in another peripheral, so a block that measures correctly against the wrong
 * clocks would pass every FREQME register check. The register holds the low 20
 * bits of the devicetree connection cookie, so the expected value is computed
 * from the devicetree rather than captured.
 *
 * What the clock_monitor API reports, which is the control: a frequency close
 * to what the target clock is known to run at, and callbacks that arrive.
 */

#ifndef FREQME_PM_H_
#define FREQME_PM_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_monitor.h>

#if !DT_NODE_HAS_STATUS(DT_NODELABEL(freqme0), okay)
#error "freqme0 is not enabled on this board"
#endif

#if !defined(CONFIG_CLOCK_MONITOR_NXP_FREQME)
#error "CONFIG_CLOCK_MONITOR_NXP_FREQME is not enabled, so there is no driver to test"
#endif

/** The single FREQME instance every part in scope has. */
#define FREQME_NODE DT_NODELABEL(freqme0)

/** The rate the default target, FRO_12M, runs at on every board in scope. */
#define FREQME_TARGET_HZ 12000000U

/** Accepted deviation of a measurement from @ref FREQME_TARGET_HZ, in percent. */
#define FREQME_TOLERANCE_PCT 5U

/** The measurement window every test uses: long enough to be accurate, short
 * enough that a test waiting on many of them stays quick.
 */
#define FREQME_WINDOW_NS 1000000U

/**
 * A window long enough that a test can observe the block while it is still
 * measuring: 200 ms, against a 1 ms window used everywhere else.
 */
#define FREQME_LONG_WINDOW_NS 200000000U

/** The register image a configuration leaves in the block. */
struct freqme_image {
	/**
	 * The configuration fields of CTRLSTAT: REF_SCALE, PULSE_MODE, PULSE_POL
	 * and the three interrupt enables. Whether the block is measuring is read
	 * separately, with freqme_measuring() and freqme_continuous().
	 */
	uint32_t ctrl;
	uint32_t min;
	uint32_t max;
	/** INPUTMUX FREQMEAS_REF and FREQMEAS_TAR. */
	uint32_t route_ref;
	uint32_t route_tar;
};

/** @brief The FREQME device. */
const struct device *freqme_dev(void);

/** @brief Sample @ref freqme_image. */
void freqme_image_read(struct freqme_image *img);

/** @brief True when two images are the same. */
bool freqme_image_equal(const struct freqme_image *a, const struct freqme_image *b);

/** @brief True while CTRLSTAT[MEASURE_IN_PROGRESS] is set. */
bool freqme_measuring(void);

/** @brief True while CTRLSTAT[CONTINUOUS_MODE_EN] is set. */
bool freqme_continuous(void);

/** @brief The three interrupt-enable bits of CTRLSTAT. */
uint32_t freqme_int_enables(void);

/** @brief The raw MIN register. */
uint32_t freqme_min(void);

/** @brief Write MIN behind the driver's back. */
void freqme_min_write(uint32_t value);

/**
 * @brief Overwrite every register this case observes with values no
 * configuration in this case produces.
 *
 * What stands in for a power cycle in a layer where nothing really cut power, so
 * that a TURN_ON has something to put back.
 *
 * @param clobbered read back after the write, so a caller can prove the clobber took
 */
void freqme_clobber(struct freqme_image *clobbered);

/**
 * @brief The route values the devicetree says the default sources produce.
 *
 * @param ref the expected FREQMEAS_REF value
 * @param tar the expected FREQMEAS_TAR value
 */
void freqme_default_route(uint32_t *ref, uint32_t *tar);

/** @brief The set_source() cookie for the FRO_12M reference, and its route value. */
uint32_t freqme_alt_ref_cookie(void);
uint32_t freqme_alt_ref_route(void);

/** @brief Turn on the clocks of the sources this case selects. */
void freqme_enable_source_clocks(void);

/**
 * @brief One MEASURE run: configure, start, wait for the callback.
 *
 * @param window_ns the measurement window
 * @param hz        the reported frequency, written on success
 *
 * @return 0, a negative errno from the API, or -ETIMEDOUT when no callback came.
 */
int freqme_measure_once(uint32_t window_ns, uint32_t *hz);

/** @brief Configure MEASURE without starting it. */
int freqme_configure_measure(uint32_t window_ns);

/** @brief Start a configured MEASURE and wait for its callback. */
int freqme_start_and_wait(uint32_t timeout_ms);

/** @brief Start a configured MEASURE without waiting. */
int freqme_measure_start(void);

/**
 * @brief Wait for the callback of a MEASURE started with freqme_measure_start().
 *
 * @return 0, or -ETIMEDOUT after stopping the measurement.
 */
int freqme_measure_wait(uint32_t timeout_ms);

/** @brief True when @p hz is within @ref FREQME_TOLERANCE_PCT of the target. */
bool freqme_rate_plausible(uint32_t hz);

/**
 * @brief Configure and start WINDOW monitoring.
 *
 * @param faulty when true the expected rate is twice the target's, so every
 *        window reports FREQ_LOW and the callback count shows whether the
 *        hardware is monitoring at all; when false the expected rate is the
 *        target's own and no callback is expected
 */
int freqme_start_window(bool faulty);

/** @brief Number of WINDOW callbacks since the last reset. */
uint32_t freqme_window_events(void);

/** @brief Reset the WINDOW callback count. */
void freqme_window_events_reset(void);

/**
 * @brief True when at least one WINDOW callback arrives within @p timeout_ms.
 *
 * Resets the count first, so only new callbacks are counted.
 */
bool freqme_window_events_arrive(uint32_t timeout_ms);

/** @brief Print what the devicetree asked for, once, from the suite setup. */
void freqme_report_configuration(void);

/** @brief Print the register image. */
void freqme_report_hw(const char *when);

#endif /* FREQME_PM_H_ */
