/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/dt-bindings/regulator/nxp_vref.h>
#include <zephyr/ztest.h>

#include "vref.h"

ZTEST(vref_pm, test_control_enable)
{
	regulator_mode_t mode;

	zassert_false(regulator_is_enabled(VREF_DEV), "another consumer holds the test regulator");
	zassert_ok(regulator_get_mode(VREF_DEV, &mode));
	zassert_equal(mode, DT_PROP_OR(VREF_NODE, regulator_initial_mode, NXP_VREF_MODE_STANDBY));
	zassert_ok(regulator_enable(VREF_DEV));
	zassert_true(vref_config_bits_present(), "DT configuration missing: CSR 0x%08x",
		     vref_csr());
	zassert_true(vref_output_stable(), "bandgap not enabled and stable");
	zassert_true(vref_mode_is_configured(), "buffer mode not applied");
	vref_report_hw("enabled");

	zassert_ok(regulator_disable(VREF_DEV));
	zassert_true(vref_output_disabled(), "last disable left analog circuitry enabled");
	zassert_ok(regulator_enable(VREF_DEV));
	zassert_true(vref_mode_is_configured(), "disable/enable lost the requested mode");
	zassert_ok(regulator_disable(VREF_DEV));
}
