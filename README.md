# zephyr-device-pm-test

Out-of-tree test harness for **Zephyr device power management (device PM)** on NXP MCUs.

Many NXP device drivers in Zephyr either do not implement device PM, or implement it
but have never been exercised, so it is unknown whether the PM hooks actually work.
This project builds one focused sample per driver that drives the driver through its
PM state machine and reports, in a greppable format, whether each transition behaves.

- **Reference target board:** `frdm_mcxn947/mcxn947/cpu0` (NXP FRDM-MCXN947).
- **Cases so far:**
  - LPADC (`nxp,lpadc`, driver `drivers/adc/adc_mcux_lpadc.c`)
  - LPCMP (`nxp,lpcmp`, driver `drivers/comparator/comparator_nxp_lpcmp.c`)

## Layout

```
samples/
  lpadc/            # LPADC device-PM test (first case)
  lpcmp/            # LPCMP comparator device-PM test
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

The LPCMP case follows the same four layers — swap `samples/lpadc` for `samples/lpcmp`,
or use `scripts/run_lpcmp.sh <baseline|device|runtime|system> [--loopback] [--flash]`.

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
2. Point the DT node at the new peripheral; adjust `prj.conf` (`CONFIG_<DRIVER>=y`).
3. Rewrite the `exercise_*()` / phase bodies to perform one real, observable operation on
   the driver (a read, a transfer, a toggle) so that "device suspended" vs "device
   active" is distinguishable at runtime. If the driver's API cannot fail on a suspended
   device — which is common — assert on the peripheral register the PM callback touches,
   the way `samples/lpcmp` checks `CCR0.CMP_EN`.
4. Keep the PM overlays (`device`, `runtime`, `system`) — they are driver-agnostic.
5. Update `testcase.yaml`.

## What each case verifies

- `samples/lpadc/README.md` — notably that the LPADC read path does **not** call
  `pm_device_runtime_get/put`, so a caller must wrap reads itself when runtime PM is on.
- `samples/lpcmp/README.md` — two driver defects: the comparator boots with
  `CCR0.CMP_EN` set while PM reports SUSPENDED, and `set_trigger_callback()` re-enables a
  suspended comparator behind PM's back.
