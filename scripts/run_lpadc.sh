#!/usr/bin/env bash
# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0
#
# Build (and optionally flash) the LPADC device-PM test in one of its PM layers.
# Run from the west workspace root, with the Zephyr environment active.
#
#   scripts/run_lpadc.sh <baseline|device|runtime|system> [--flash]

set -euo pipefail

BOARD="frdm_mcxn947/mcxn947/cpu0"
SAMPLE="$(cd "$(dirname "$0")/.." && pwd)/samples/lpadc"
MODE="${1:-baseline}"
FLASH="${2:-}"

case "$MODE" in
  baseline)
    EXTRA=()
    ;;
  device)
    EXTRA=(-- -DEXTRA_CONF_FILE=overlay-pm-device.conf)
    ;;
  runtime)
    EXTRA=(-- -DEXTRA_CONF_FILE=overlay-pm-runtime.conf)
    ;;
  system)
    EXTRA=(-- -DEXTRA_CONF_FILE=overlay-pm-system.conf
           "-DDTC_OVERLAY_FILE=app.overlay;constraints.overlay")
    ;;
  *)
    echo "usage: $0 <baseline|device|runtime|system> [--flash]" >&2
    exit 2
    ;;
esac

west build -b "$BOARD" "$SAMPLE" -p always "${EXTRA[@]}"

if [ "$FLASH" = "--flash" ]; then
  west flash
fi
