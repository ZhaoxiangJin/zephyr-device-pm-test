/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The control: this case really does drive the FREQME.
 *
 * Compiled into every layer. If this fails, no verdict from the layer under test
 * means anything.
 *
 * Four claims: that a measurement reports a frequency close to the target's, that
 * the INPUTMUX route holds what the devicetree asked for, that a configuration
 * leaves a register image distinguishable from a reset block, and that WINDOW
 * monitoring is silent on a good clock and reports on a bad one. Every later layer
 * tells "restored" from "came back from reset" by comparing against that image,
 * and tells "monitoring" from "stopped" by whether WINDOW callbacks arrive.
 */

#include <zephyr/drivers/clock_monitor.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#include "freqme.h"

ZTEST(freqme_pm, test_control_measures_and_monitors)
{
	const struct device *dev = freqme_dev();
	struct freqme_image configured;
	uint32_t want_ref;
	uint32_t want_tar;
	uint32_t hz = 0U;
	bool held = false;
	int measure_rc;
	bool quiet_when_good;
	bool reports_when_bad;
	bool measuring_window;
	bool continuous_window;
	bool measuring_after_stop;

	/*
	 * Under runtime PM the device boots suspended and this driver takes no
	 * reference of its own, so the control has to ask for it. That an
	 * unwrapped call is refused is test_runtime.c's claim, not a premise of
	 * the control.
	 */
	if (pm_device_runtime_is_enabled(dev)) {
		zassert_ok(pm_device_runtime_get(dev),
			   "%s: could not resume the device for the control", dev->name);
		held = true;
	}

	measure_rc = freqme_measure_once(FREQME_WINDOW_NS, &hz);
	freqme_image_read(&configured);
	freqme_report_hw("control, measured");

	zassert_ok(freqme_start_window(false), "%s: could not start WINDOW", dev->name);
	measuring_window = freqme_measuring();
	continuous_window = freqme_continuous();
	quiet_when_good = !freqme_window_events_arrive(50U);
	(void)clock_monitor_stop(dev);

	zassert_ok(freqme_start_window(true), "%s: could not start a faulty WINDOW", dev->name);
	reports_when_bad = freqme_window_events_arrive(50U);
	(void)clock_monitor_stop(dev);
	measuring_after_stop = freqme_measuring();

	if (held) {
		/* Before the assertions: a failure must not leak the reference. */
		(void)pm_device_runtime_put(dev);
	}

	freqme_default_route(&want_ref, &want_tar);

	/*
	 * The route first. A block measuring against the wrong inputs would still
	 * produce a register image and possibly a number.
	 */
	zassert_equal(configured.route_ref, want_ref,
		      "%s: FREQMEAS_REF holds %u, the devicetree default source is %u",
		      dev->name, configured.route_ref, want_ref);
	zassert_equal(configured.route_tar, want_tar,
		      "%s: FREQMEAS_TAR holds %u, the devicetree default source is %u",
		      dev->name, configured.route_tar, want_tar);

	zassert_ok(measure_rc, "%s: the measurement failed (%d)", dev->name, measure_rc);
	zassert_true(freqme_rate_plausible(hz), "%s: measured %u Hz, want %u Hz +/- %u%%",
		     dev->name, hz, FREQME_TARGET_HZ, FREQME_TOLERANCE_PCT);

	/*
	 * A reset block has REF_SCALE 0 and no interrupt enabled; a 1 ms window
	 * against any reference in scope needs a REF_SCALE well above 0 and MEASURE
	 * enables the result-ready interrupt. So the image is distinguishable.
	 */
	zassert_not_equal(configured.ctrl, 0U, "%s: the configuration left CTRLSTAT at 0",
			  dev->name);

	zassert_true(measuring_window && continuous_window,
		     "%s: WINDOW started but the block is not measuring continuously",
		     dev->name);
	zassert_true(quiet_when_good, "%s: WINDOW reported a good clock as out of range",
		     dev->name);
	zassert_true(reports_when_bad, "%s: WINDOW did not report a clock at half the "
		     "expected rate", dev->name);
	zassert_false(measuring_after_stop, "%s: still measuring after clock_monitor_stop()",
		      dev->name);
}
