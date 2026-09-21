/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The LPADC itself: the wiring, and the one observation this case makes.
 */

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#include "lpadc.h"

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) ||                                                       \
	!DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "This board has no LPADC wiring: add boards/<board_target>.overlay with zephyr,user io-channels"
#endif

#define DT_SPEC_AND_COMMA_FOR_INPUTS(node_id, prop, idx)                                           \
	COND_CODE_1(DT_PHA_HAS_CELL_AT_IDX(node_id, prop, idx, input),                              \
		    (ADC_DT_SPEC_GET_BY_IDX(node_id, idx),), ())

const struct adc_dt_spec lpadc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, DT_SPEC_AND_COMMA_FOR_INPUTS)
};

int lpadc_channels_setup(void)
{
	for (size_t i = 0U; i < ARRAY_SIZE(lpadc_channels); i++) {
		int err;

		if (!adc_is_ready_dt(&lpadc_channels[i])) {
			return -ENODEV;
		}

		err = adc_channel_setup_dt(&lpadc_channels[i]);
		if (err < 0) {
			return err;
		}
	}

	return 0;
}

int lpadc_read(int32_t *out_raw)
{
	int16_t buf;
	struct adc_sequence sequence = {
		.buffer = &buf,
		.buffer_size = sizeof(buf),
	};
	int err;

	err = adc_sequence_init_dt(&lpadc_channels[0], &sequence);
	if (err < 0) {
		return err;
	}

	err = adc_read_dt(&lpadc_channels[0], &sequence);
	if (err < 0) {
		return err;
	}

	*out_raw = buf;
	return 0;
}
