#!/usr/bin/env bash
# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0
#
# Build (and optionally flash) the LPADC device-PM test in one of its PM layers.
# Run from the west workspace root, with the Zephyr environment active.
#
#   scripts/run_lpadc.sh <baseline|device|runtime|system> [-b BOARD] [--flash]
#
# BOARD defaults to frdm_mcxn947/mcxn947/cpu0. Any board target with an overlay in
# samples/lpadc/boards/ works; see samples/lpadc/README.md for the list.

set -euo pipefail

BOARD="frdm_mcxn947/mcxn947/cpu0"
SAMPLE="$(cd "$(dirname "$0")/.." && pwd)/samples/lpadc"
MODE="${1:-baseline}"
shift || true

FLASH=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    -b|--board) BOARD="${2:?-b needs a board target}"; shift 2 ;;
    --flash)    FLASH=1; shift ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

case "$MODE" in
  baseline) EXTRA=() ;;
  device)   EXTRA=(-DEXTRA_CONF_FILE=overlay-pm-device.conf) ;;
  runtime)  EXTRA=(-DEXTRA_CONF_FILE=overlay-pm-runtime.conf) ;;
  system)
    # EXTRA_DTC_OVERLAY_FILE is additive, so boards/<target>.overlay is still
    # discovered automatically.
    EXTRA=(-DEXTRA_CONF_FILE=overlay-pm-system.conf
           -DEXTRA_DTC_OVERLAY_FILE=constraints.overlay)
    ;;
  *)
    echo "usage: $0 <baseline|device|runtime|system> [-b BOARD] [--flash]" >&2
    exit 2
    ;;
esac

if [ "${#EXTRA[@]}" -gt 0 ]; then
  west build -b "$BOARD" "$SAMPLE" -p always -- "${EXTRA[@]}"
else
  west build -b "$BOARD" "$SAMPLE" -p always
fi

if [ "$FLASH" = "1" ]; then
  west flash
fi
