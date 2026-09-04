# zephyr-device-pm-test

Out-of-tree test harness for **Zephyr device power management (device PM)** on NXP MCUs.

Many NXP device drivers in Zephyr either do not implement device PM, or implement it
but have never been exercised, so it is unknown whether the PM hooks actually work.
This project builds one focused sample per driver that drives the driver through its
PM state machine and reports, in a greppable format, whether each transition behaves.

- **Reference target board:** `frdm_mcxn947/mcxn947/cpu0` (NXP FRDM-MCXN947).
- **Cases so far:**
  - LPADC (`nxp,lpc-lpadc`, driver `drivers/adc/adc_mcux_lpadc.c`)
  - LPCMP (`nxp,lpcmp`, driver `drivers/comparator/comparator_nxp_lpcmp.c`)

## Supported boards

Both cases are enabled across the whole MCXN and MCXA line. Every board target below
has an overlay in the sample's `boards/` directory, which Zephyr picks up automatically
from the board target name — nothing extra to pass on the command line.

| Family | Board targets |
| --- | --- |
| MCXN | `frdm_mcxn947/mcxn947/cpu0`, `frdm_mcxn947/mcxn947/cpu0/qspi`, `frdm_mcxn236`, `mcx_n9xx_evk/mcxn947/cpu0`, `mcx_n9xx_evk/mcxn947/cpu0/qspi`, `mcx_n5xx_evk/mcxn547/cpu0` |
| MCXA | `frdm_mcxa153`, `frdm_mcxa156`, `frdm_mcxa266`, `frdm_mcxa344`, `frdm_mcxa346`, `frdm_mcxa366`, `frdm_mcxa577` |

Every one of these SoCs `select HAS_PM` and declares the same four power states
(`sleep`, `deepsleep`, `powerdown`, `deeppowerdown`), so the PM `.conf` overlays and
`constraints.overlay` are family-wide; only the peripheral wiring is per-board.

Not covered: the `cpu1` clusters and the `_ns`/TrustZone board variants. Both would need
a different DT address map and, for `cpu1`, its own board glue; neither is exercised by
the in-tree LPADC/LPCMP tests either.

## Layout

```
samples/
  lpadc/            # LPADC device-PM test (first case)
    boards/         #   per-board channel + reference wiring
  lpcmp/            # LPCMP comparator device-PM test
    boards/         #   per-board mux input, DAC and loopback GPIO
scripts/            # build/flash helpers
```

## Prerequisites

This is a **freestanding Zephyr application**. It has no `west.yml` of its own and does
not vendor Zephyr; it builds against the Zephyr already present in your west workspace.

- Zephyr **v4.4.x** (developed against `4.4.99`, tip of tree at time of writing).
- The NXP HAL module (`hal_nxp`) that ships with the Zephyr manifest.
- A working Zephyr build environment (`west`, Zephyr SDK).

Clone this repo *inside* your west workspace, next to `zephyr/`:

```
zephyrproject/
  zephyr/
  zephyr-device-pm-test/   <-- here
```

## Building & running

From the workspace root (or anywhere with the west environment active). Substitute any
board target from the table above for `$BOARD`:

```sh
BOARD=frdm_mcxn947/mcxn947/cpu0

# Baseline: no PM, just prove the sample reads the ADC.
west build -b $BOARD zephyr-device-pm-test/samples/lpadc -p always

# Device PM (manual suspend/resume via pm_device_action_run).
west build -b $BOARD zephyr-device-pm-test/samples/lpadc -p always \
  -- -DEXTRA_CONF_FILE=overlay-pm-device.conf

# Runtime device PM (get/put reference counting).
west build -b $BOARD zephyr-device-pm-test/samples/lpadc -p always \
  -- -DEXTRA_CONF_FILE=overlay-pm-runtime.conf

# System PM + device power-state constraints. EXTRA_DTC_OVERLAY_FILE is additive, so
# boards/<board_target>.overlay is still applied.
west build -b $BOARD zephyr-device-pm-test/samples/lpadc -p always \
  -- -DEXTRA_CONF_FILE=overlay-pm-system.conf \
     -DEXTRA_DTC_OVERLAY_FILE=constraints.overlay

west flash
```

Or use the helper:
`scripts/run_lpadc.sh <baseline|device|runtime|system> [-b BOARD] [--flash]`.

The LPCMP case follows the same four layers — swap `samples/lpadc` for `samples/lpcmp`,
or use
`scripts/run_lpcmp.sh <baseline|device|runtime|system> [-b BOARD] [--loopback] [--flash]`.

To build-sweep every board target at once:

```sh
west twister -T zephyr-device-pm-test/samples --build-only \
  -p frdm_mcxn947/mcxn947/cpu0 -p frdm_mcxa153 ...   # or --all
```

## Reading the output

Every phase prints a line prefixed `PM-TEST:`. A run ends with either
`PM-TEST: RESULT PASS` or `PM-TEST: RESULT FAIL`. Twister (`testcase.yaml`) keys off
these strings, so no debugger is needed to judge a run.

A `RESULT FAIL` is a legitimate, informative outcome: it usually means the harness found
a real driver defect, not that the harness is broken. Each sample's README states which
layers are expected to fail today and why, so a driver fix is visible as those layers
turning green.

## Watch out for: runtime PM has to be *enabled*, not just compiled in

`CONFIG_PM_DEVICE_RUNTIME=y` on its own is not enough to test anything.
`pm_device_driver_init()` only leaves a device SUSPENDED and marks runtime PM enabled for
it when `CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE=y` or the node carries
`zephyr,pm-device-runtime-auto`. Without one of those, the device is resumed to ACTIVE,
runtime PM stays *disabled* for it, and `pm_device_runtime_get()`/`put()` return 0
without doing anything — a runtime phase that passes while exercising nothing. The LPCMP
overlays set `CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE=y` for this reason;
`samples/lpadc/overlay-pm-runtime.conf` predates this finding and still needs it.

## Adding a new driver case

1. `cp -r samples/lpcmp samples/<driver>` (or `samples/lpadc`).
2. Point the DT node at the new peripheral in `boards/<board_target>.overlay` — one file
   per board you support; Zephyr matches them by board target name. Adjust `prj.conf`
   (`CONFIG_<DRIVER>=y`).
3. Rewrite the `exercise_*()` / phase bodies to perform one real, observable operation on
   the driver (a read, a transfer, a toggle) so that "device suspended" vs "device
   active" is distinguishable at runtime. If the driver's API cannot fail on a suspended
   device — which is common — assert on the peripheral register the PM callback touches,
   the way `samples/lpcmp` checks `CCR0.CMP_EN`.
4. Keep the PM overlays (`device`, `runtime`, `system`) — they are driver-agnostic.
5. Update `testcase.yaml`, including `platform_allow`.

## What each case verifies

- `samples/lpadc/README.md` — notably that the LPADC read path does **not** call
  `pm_device_runtime_get/put`, so a caller must wrap reads itself when runtime PM is on,
  and that the driver does not even *compile* with `CONFIG_PM_DEVICE=y` when two LPADC
  instances are enabled (`LPADC_PM_DEVICE_DEFINE` is not instance-parameterized).
- `samples/lpcmp/README.md` — two driver defects: the comparator boots with
  `CCR0.CMP_EN` set while PM reports SUSPENDED, and `set_trigger_callback()` re-enables a
  suspended comparator behind PM's back.
