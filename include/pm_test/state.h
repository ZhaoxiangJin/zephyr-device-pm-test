/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Asserting what state a device is in.
 *
 * A device-PM test is mostly a sequence of "act, then assert the state", so this
 * is the assertion the cases spend most of their lines on. Going through one
 * macro is what makes a failure say which device, what was expected and what was
 * found -- the four cases each used to print the state and then fail a separate
 * unnamed check, which named neither.
 */

#ifndef PM_TEST_STATE_H_
#define PM_TEST_STATE_H_

#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>

/**
 * @brief Assert that @p dev reports power state @p expected.
 *
 * @param dev      device to query
 * @param expected the @ref pm_device_state it must be in
 */
#define zassert_pm_state(dev, expected)                                                            \
	do {                                                                                       \
		enum pm_device_state _pm_st;                                                       \
		int _pm_rc = pm_device_state_get((dev), &_pm_st);                                  \
                                                                                                   \
		zassert_ok(_pm_rc, "%s: pm_device_state_get() failed (%d)", (dev)->name, _pm_rc);   \
		zassert_equal(_pm_st, (expected), "%s: expected %s, got %s", (dev)->name,          \
			      pm_device_state_str(expected), pm_device_state_str(_pm_st));         \
	} while (0)

/**
 * @brief Note the state of @p dev in the log without judging it.
 *
 * For a state that is worth having in the captured log but is not what the test
 * is asserting -- typically the state a device booted in, before the test has
 * done anything to it.
 *
 * @param dev  device to query
 * @param when short label for where in the sequence this is, e.g. "at boot"
 */
#if defined(CONFIG_PM_DEVICE)
#define pm_test_note_state(dev, when)                                                              \
	do {                                                                                       \
		enum pm_device_state _pm_st;                                                        \
                                                                                                   \
		if (pm_device_state_get((dev), &_pm_st) == 0) {                                     \
			TC_PRINT("%s %s: %s\n", (dev)->name, (when),                                \
				 pm_device_state_str(_pm_st));                                      \
		}                                                                                  \
	} while (0)
#else
/*
 * The baseline layer has no device PM at all, and pm_device_state_str() is
 * compiled only with CONFIG_PM_DEVICE. A case that notes the boot state from its
 * suite setup -- which every layer shares -- would otherwise fail to link there,
 * so say what the build is instead of dropping the line.
 */
#define pm_test_note_state(dev, when)                                                              \
	do {                                                                                       \
		TC_PRINT("%s %s: no device PM in this build\n", (dev)->name, (when));               \
	} while (0)
#endif /* CONFIG_PM_DEVICE */

#endif /* PM_TEST_STATE_H_ */
