# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Expose the shared PM-test library and layer snippets.
list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_LIST_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(vref_device_pm)

target_sources(app PRIVATE src/main.c src/vref.c src/test_control.c)

target_sources_ifdef(CONFIG_PM_TEST_LAYER_DEVICE     app PRIVATE src/test_device.c)
target_sources_ifdef(CONFIG_PM_TEST_LAYER_RUNTIME    app PRIVATE src/test_runtime.c)
target_sources_ifdef(CONFIG_PM_TEST_LAYER_SYSTEM     app PRIVATE src/test_system.c)
target_sources_ifdef(CONFIG_PM_TEST_LAYER_SYSMANAGED app PRIVATE src/test_lowpower.c)
target_sources_ifdef(CONFIG_PM_TEST_LAYER_DPD        app PRIVATE src/test_lowpower.c)
