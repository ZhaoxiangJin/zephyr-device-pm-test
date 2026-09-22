/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The control: this case really does drive the reference.
 *
 * Compiled into every layer. If this fails, no verdict from the layer under test
 * means anything -- which is the whole reason the baseline layer exists as a layer.
 *
 * Deliberately not part of the control: whether the block carries the
 * configuration the devicetree asked for. That is a per-layer claim, because the
 * bits are written by configure_hw() on TURN_ON and there are layers in which the
 * TURN_ON never arrives -- see src/test_runtime.c and
 * vref-unconfigured-under-runtime-pm in docs/findings.md. A reference that is
 * merely unconfigured still produces a usable output, so the control below passes
 * in those layers, and that is what makes the finding a finding rather than a
 * broken case.
 */

#include <zephyr/ztest.h>

#include "vref.h"

ZTEST(vref_pm, test_control_enable)
{
	int rc;

	zassert_true(vref_mode_is_configured(),
		     "mode does not read back regulator-initial-mode (CSR 0x%08x)", vref_csr());

	/*
	 * regulator_enable() spins on CSR[VREFST] itself, so a bandgap that never
	 * settles hangs here rather than failing. A run that stops on this line is
	 * that, not a crash.
	 */
	rc = regulator_enable(VREF_DEV);
	zassert_ok(rc, "regulator_enable() failed (%d)", rc);
	zassert_true(vref_output_stable(), "output does not read stable once enabled");
	vref_report_hw("enabled");

	/* Released again: every later test says for itself whether it needs a
	 * consumer's reference, so none of them inherits one from here.
	 */
	zassert_ok(regulator_disable(VREF_DEV), "regulator_disable() failed");
}
