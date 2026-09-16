#!/usr/bin/env bash
# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0
#
# Build (and optionally flash) the VREF device-PM test in one of its PM layers.
# Run from the west workspace root, with the Zephyr environment active.
#
#   scripts/run_vref.sh <baseline|device|runtime|system|sysmanaged|dpd> [-b BOARD] [--flash]
#
# BOARD defaults to frdm_mcxn947/mcxn947/cpu0. Any board target with an overlay in
# samples/vref/boards/ works; see samples/vref/README.md for the list and for the
# two checks the dpd mode is expected to fail. There is no MCXA target: no MCXA SoC
# has an nxp,vref node.

set -euo pipefail

BOARD="frdm_mcxn947/mcxn947/cpu0"
SAMPLE="$(cd "$(dirname "$0")/.." && pwd)/samples/vref"
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
  sysmanaged) EXTRA=(-DEXTRA_CONF_FILE=overlay-pm-sysmanaged.conf) ;;
  dpd)
    # Only SRAMA survives Deep Power Down on MCXN, so the overlay also narrows
    # sram0 to the retained window.
    case "$BOARD" in
      *mcxn*) ;;
      *) echo "dpd: vref only exists on MCXN, not on '$BOARD'" >&2; exit 2 ;;
    esac
    EXTRA=(-DEXTRA_CONF_FILE=overlay-pm-sysmanaged.conf
           -DEXTRA_DTC_OVERLAY_FILE=dpd-mcxn.overlay)
    ;;
  *)
    echo "usage: $0 <baseline|device|runtime|system|sysmanaged|dpd> [-b BOARD] [--flash]" >&2
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
