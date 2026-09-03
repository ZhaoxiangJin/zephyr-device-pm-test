# zephyr-device-pm-test

Out-of-tree test harness for **Zephyr device power management (device PM)** on NXP MCUs.

Many NXP device drivers in Zephyr either do not implement device PM, or implement it
but have never been exercised, so it is unknown whether the PM hooks actually work.
This project builds one focused sample per driver that drives the driver through its
PM state machine and reports, in a greppable format, whether each transition behaves.

- **Reference target board:** `frdm_mcxn947/mcxn947/cpu0` (NXP FRDM-MCXN947).
- **First case:** LPADC (`nxp,lpadc`, driver `drivers/adc/adc_mcux_lpadc.c`).

## Layout

```
samples/
  lpadc/            # LPADC device-PM test (first case)
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

From the workspace root (or anywhere with the west environment active):

```sh
# Baseline: no PM, just prove the sample reads the ADC.
west build -b frdm_mcxn947/mcxn947/cpu0 zephyr-device-pm-test/samples/lpadc -p always

# Device PM (manual suspend/resume via pm_device_action_run).
west build -b frdm_mcxn947/mcxn947/cpu0 zephyr-device-pm-test/samples/lpadc -p always \
  -- -DEXTRA_CONF_FILE=overlay-pm-device.conf

# Runtime device PM (get/put reference counting).
west build -b frdm_mcxn947/mcxn947/cpu0 zephyr-device-pm-test/samples/lpadc -p always \
  -- -DEXTRA_CONF_FILE=overlay-pm-runtime.conf

# System PM + device power-state constraints.
west build -b frdm_mcxn947/mcxn947/cpu0 zephyr-device-pm-test/samples/lpadc -p always \
  -- "-DEXTRA_CONF_FILE=overlay-pm-system.conf;-DDTC_OVERLAY_FILE=app.overlay;constraints.overlay"

west flash
```

Or use the helper: `scripts/run_lpadc.sh <baseline|device|runtime|system>`.

## Reading the output

Every phase prints a line prefixed `PM-TEST:`. A run ends with either
`PM-TEST: RESULT PASS` or `PM-TEST: RESULT FAIL`. Twister (`testcase.yaml`) keys off
these strings, so no debugger is needed to judge a run.

## Adding a new driver case

1. `cp -r samples/lpadc samples/<driver>`.
2. Point the DT node / io-channels at the new peripheral; adjust `prj.conf` (`CONFIG_<DRIVER>=y`).
3. Rewrite `src/main.c` `exercise_device()` to perform one real, observable operation on the
   driver (a read, a transfer, a toggle) so that "device suspended" vs "device active" is
   distinguishable at runtime.
4. Keep the four PM overlays (`device`, `runtime`, `system`) — they are driver-agnostic.
5. Update `testcase.yaml`.

## What the LPADC case verifies

See `samples/lpadc/README.md` for the per-phase expectations, including the notable finding
that the LPADC driver's read path does **not** call `pm_device_runtime_get/put`, so a caller
must wrap reads itself when runtime PM is enabled.
