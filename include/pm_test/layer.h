/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The layer under test, and the premises it is entitled to assume.
 *
 * Every case includes this header exactly once, from its suite file. The build
 * assertions below are the point of it: each one is a way for a build to come out
 * green while proving nothing, and each was a paragraph of prose in this
 * repository's README before it was a build failure.
 * docs/zephyr-pm-notes.md keeps the reasoning for whoever trips one.
 */

#ifndef PM_TEST_LAYER_H_
#define PM_TEST_LAYER_H_

#include <zephyr/devicetree.h>
#include <zephyr/toolchain.h>

/** The layer name, spelled as docs/pm-layers.md and the reports under results/ spell it. */
#define PM_TEST_LAYER CONFIG_PM_TEST_LAYER_NAME

/*
 * CONFIG_ZTEST_NO_YIELD, which the library turns on to keep the core out of WFI,
 * spins with interrupts locked once the run is over. A deferred log's thread would
 * never run again from there, so the run would capture as truncated -- a green
 * board reported as a hang.
 */
BUILD_ASSERT(!IS_ENABLED(CONFIG_LOG) || IS_ENABLED(CONFIG_LOG_MODE_IMMEDIATE),
	     "CONFIG_LOG without CONFIG_LOG_MODE_IMMEDIATE: log output would be lost once the "
	     "run ends and interrupts stay locked");

#if defined(CONFIG_PM_TEST_LAYER_RUNTIME)
BUILD_ASSERT(IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE),
	     "runtime layer without PM_DEVICE_RUNTIME_DEFAULT_ENABLE: runtime PM is compiled in "
	     "but not enabled on any device, so get()/put() return 0 without reaching the driver");
#endif

#if defined(CONFIG_PM_TEST_LAYER_SYSMANAGED) || defined(CONFIG_PM_TEST_LAYER_DPD)
BUILD_ASSERT(IS_ENABLED(CONFIG_PM),
	     "sysmanaged/dpd layer without CONFIG_PM: there is no device sweep to observe");
BUILD_ASSERT(!IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME),
	     "sysmanaged/dpd layer with PM_DEVICE_RUNTIME: pm_suspend_devices() skips every device "
	     "that runtime PM owns, so the sweep never reaches the device under test");
BUILD_ASSERT(IS_ENABLED(CONFIG_PM_DEVICE_SYSTEM_MANAGED),
	     "sysmanaged/dpd layer without PM_DEVICE_SYSTEM_MANAGED: same, no sweep");
#endif

#if defined(CONFIG_PM_TEST_LAYER_DPD)
BUILD_ASSERT(DT_NODE_HAS_STATUS(DT_NODELABEL(deeppowerdown), okay),
	     "dpd layer without an enabled deeppowerdown state: the forced transition degrades "
	     "into the sysmanaged layer's Deep Sleep and the peripheral never loses power");
BUILD_ASSERT(IS_ENABLED(CONFIG_PM_S2RAM),
	     "dpd layer without CONFIG_PM_S2RAM: same -- the state exists in the devicetree but "
	     "the architecture cannot suspend to RAM, so nothing is turned off");
#endif

/**
 * @brief ZTEST_SUITE() setup hook shared by every case.
 *
 * Names the layer in the captured log, which is the one thing a log needs to be
 * readable after the fact and cannot be recovered from the log itself.
 *
 * @return NULL; no per-suite fixture.
 */
void *pm_test_setup(void);

#endif /* PM_TEST_LAYER_H_ */
