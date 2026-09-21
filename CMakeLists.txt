# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

# Zephyr module entry point, pulled in by find_package(Zephyr) in every case that
# names this repository in EXTRA_ZEPHYR_MODULES. Everything is gated on
# CONFIG_PM_TEST so that a build which does not select the library pays nothing.

if(CONFIG_PM_TEST)
  zephyr_include_directories(include)
  add_subdirectory(lib/pm_test)
endif()
