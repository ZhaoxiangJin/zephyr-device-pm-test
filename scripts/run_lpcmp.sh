#!/usr/bin/env bash
# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0
#
# Build (and optionally flash) the LPCMP device-PM test in one of its PM layers.
# Run from the west workspace root, with the Zephyr environment active.
#
#   scripts/run_lpcmp.sh <baseline|device|runtime|system> [-b BOARD] [--loopback] [--flash]
#
# BOARD defaults to frdm_mcxn947/mcxn947/cpu0. Any board target with an overlay in
# samples/lpcmp/boards/ works; see samples/lpcmp/README.md for the list.
#
# --loopback stacks the GPIO loopback layer, which needs a jumper wire between the
# board's test GPIO and the comparator's positive input. The two pins are named in the
# header comment of samples/lpcmp/boards/<board_target>.overlay.

set -euo pipefail

BOARD="frdm_mcxn947/mcxn947/cpu0"
SAMPLE="$(cd "$(dirname "$0")/.." && pwd)/samples/lpcmp"
MODE="${1:-baseline}"
shift || true

LOOPBACK=0
FLASH=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    -b|--board) BOARD="${2:?-b needs a board target}"; shift 2 ;;
    --loopback) LOOPBACK=1; shift ;;
    --flash)    FLASH=1; shift ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

case "$MODE" in
  baseline) CONFS=() ;;
  device)   CONFS=(overlay-pm-device.conf) ;;
  runtime)  CONFS=(overlay-pm-runtime.conf) ;;
  system)   CONFS=(overlay-pm-system.conf) ;;
  *)
    echo "usage: $0 <baseline|device|runtime|system> [-b BOARD] [--loopback] [--flash]" >&2
    exit 2
    ;;
esac

if [ "$LOOPBACK" = "1" ]; then
  CONFS+=(overlay-loopback.conf)
fi

EXTRA=()
if [ "${#CONFS[@]}" -gt 0 ]; then
  # CMake list separator is ';'
  JOINED="$(IFS=';'; echo "${CONFS[*]}")"
  EXTRA+=("-DEXTRA_CONF_FILE=$JOINED")
fi

# EXTRA_DTC_OVERLAY_FILE is additive, so boards/<target>.overlay is still discovered
# automatically.
if [ "$MODE" = "system" ]; then
  EXTRA+=("-DEXTRA_DTC_OVERLAY_FILE=constraints.overlay")
fi

if [ "${#EXTRA[@]}" -gt 0 ]; then
  west build -b "$BOARD" "$SAMPLE" -p always -- "${EXTRA[@]}"
else
  west build -b "$BOARD" "$SAMPLE" -p always
fi

if [ "$FLASH" = "1" ]; then
  west flash
fi
