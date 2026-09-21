/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief What every layer of the LPADC case needs from the hardware.
 *
 * The one observable this case has is whether a conversion completes. The
 * driver's SUSPEND action calls LPADC_Enable(false), so "a read works" and "the
 * block is powered and configured" are the same question -- which is what lets
 * every layer be tested through this single operation.
 */

#ifndef LPADC_PM_H_
#define LPADC_PM_H_

#include <stdint.h>
#include <zephyr/drivers/adc.h>

/** The channels named by zephyr,user io-channels in the board overlay. */
extern const struct adc_dt_spec lpadc_channels[];

/** The LPADC controller itself: the parent of those channels. */
#define LPADC_DEV (lpadc_channels[0].dev)

/**
 * @brief Configure every channel this board wired up.
 *
 * Also exercised as a test in its own right by the runtime layer, which calls it
 * against a suspended device.
 *
 * @return 0 on success, negative errno from adc_channel_setup_dt() otherwise.
 */
int lpadc_channels_setup(void);

/**
 * @brief Perform one conversion on channel 0.
 *
 * @param out_raw where to store the raw sample; untouched on failure.
 *
 * @return 0 on a completed conversion, negative errno otherwise.
 */
int lpadc_read(int32_t *out_raw);

#endif /* LPADC_PM_H_ */
