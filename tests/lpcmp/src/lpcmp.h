/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief What every layer of the LPCMP case needs from the hardware.
 *
 * The comparator API cannot see a suspended comparator: comparator_get_output()
 * returns the CSR.COUT latch, so against a disabled block it yields a stale level
 * rather than an error. CCR0.CMP_EN is therefore the observable -- it is the one
 * bit the driver's PM callback touches -- and the optional loopback layer is what
 * turns "the bit is clear" into "the analog comparison really stopped".
 */

#ifndef LPCMP_PM_H_
#define LPCMP_PM_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>

/** The comparator under test, named by the board overlay's test-comp alias. */
extern const struct device *const lpcmp_dev;

#define LPCMP_DEV lpcmp_dev

/**
 * @brief Is the comparator actually comparing?
 *
 * Read straight out of CCR0, not from the driver: a PM state machine that has
 * drifted away from the hardware is exactly what this case is looking for.
 */
bool lpcmp_cmp_en(void);

/**
 * @brief The input mux, hysteresis and power-mode configuration register.
 *
 * The driver has no save/restore of its own, so comparing CCR2 across a
 * suspend/resume round trip says whether the configuration survived unaided.
 */
uint32_t lpcmp_ccr2(void);

/** @brief One comparator output read, as an application would do it. */
int lpcmp_output(void);

#if defined(CONFIG_PM_TEST_LPCMP_LOOPBACK)
/**
 * @brief Configure the GPIO that drives the comparator's positive input.
 *
 * @return 0 on success, negative errno if the pin is not usable.
 */
int lpcmp_loopback_init(void);

/**
 * @brief Drive the input high and then low, and see whether the output follows.
 *
 * @return true only if the output tracked both edges, i.e. the comparator is
 *         comparing; false if it is disabled, which is what a suspended device
 *         must look like.
 */
bool lpcmp_loopback_tracks_input(void);
#endif /* CONFIG_PM_TEST_LPCMP_LOOPBACK */

#endif /* LPCMP_PM_H_ */
