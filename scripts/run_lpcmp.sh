#!/usr/bin/env bash
# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0
#
# Build (and optionally flash) the LPCMP device-PM test in one of its PM layers.
# Run from the west workspace root, with the Zephyr environment active.
#
#   scripts/run_lpcmp.sh <baseline|device|runtime|system> [--loopback] [--flash]
#
# --loopback stacks the GPIO loopback layer, which needs a jumper wire on
# FRDM-MCXN947 between J2-11 (gpio1.12) and J2-17 (CMP0_IN0).

set -euo pipefail

BOARD="frdm_mcxn947/mcxn947/cpu0"
SAMPLE="$(cd "$(dirname "$0")/.." && pwd)/samples/lpcmp"
MODE="${1:-baseline}"
shift || true

LOOPBACK=0
FLASH=0
for arg in "$@"; do
  case "$arg" in
    --loopback) LOOPBACK=1 ;;
    --flash)    FLASH=1 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

case "$MODE" in
  baseline) CONFS=() ;;
  device)   CONFS=(overlay-pm-device.conf) ;;
  runtime)  CONFS=(overlay-pm-runtime.conf) ;;
  system)   CONFS=(overlay-pm-system.conf) ;;
  *)
    echo "usage: $0 <baseline|device|runtime|system> [--loopback] [--flash]" >&2
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

# app.overlay is picked up automatically; once DTC_OVERLAY_FILE is set it must be
# listed explicitly alongside the extra overlay.
if [ "$MODE" = "system" ]; then
  EXTRA+=("-DDTC_OVERLAY_FILE=app.overlay;constraints.overlay")
fi

west build -b "$BOARD" "$SAMPLE" -p always -- "${EXTRA[@]}"

if [ "$FLASH" = "1" ]; then
  west flash
fi
