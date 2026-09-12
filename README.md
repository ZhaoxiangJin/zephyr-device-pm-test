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
    device-pm-case.json   #   which DT compatibles this case covers
  lpcmp/            # LPCMP comparator device-PM test
    boards/         #   per-board mux input, DAC and loopback GPIO
    device-pm-case.json
results/            # what has actually run, one file per run
scripts/            # build/flash helpers
```

`device-pm-case.json` and `results/` exist for a reader outside this repository:
the [Device PM report](https://zhaoxiangjin.github.io/zephyr-data/reports/device-pm/)
derives Device PM *enablement* by statically analysing the Zephyr tree, but no
amount of source analysis can say whether a PM transition was ever exercised.
That evidence only exists here, so it is recorded in a form a tool can read —
`device-pm-case.json` ties a sample to the compatibles it covers, `testcase.yaml`
already declares the board targets and the PM layers, and `results/*.json`
records the runs. See [results/README.md](results/README.md) for the format.

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

# The system-managed device sweep: pm_suspend_devices()/pm_resume_devices() around
# one forced Deep Sleep transition. Mutually exclusive with runtime PM.
west build -b $BOARD zephyr-device-pm-test/samples/lpadc -p always \
  -- -DEXTRA_CONF_FILE=overlay-pm-sysmanaged.conf

# Deep Power Down: the peripheral register block is reset, so the sweep's
# SUSPEND/RESUME is not enough and the power domain's TURN_OFF/TURN_ON has to
# restore the hardware. The DT overlay is family-specific (only SRAMA is retained
# across DPD on MCXN), so use dpd-mcxa.overlay on MCXA.
west build -b $BOARD zephyr-device-pm-test/samples/lpadc -p always \
  -- -DEXTRA_CONF_FILE=overlay-pm-sysmanaged.conf \
     -DEXTRA_DTC_OVERLAY_FILE=dpd-mcxn.overlay

west flash
```

Or use the helper:
`scripts/run_lpadc.sh <baseline|device|runtime|system|sysmanaged|dpd> [-b BOARD] [--flash]`
(the `dpd` mode picks the family overlay from the board target).

The LPCMP case follows the first four layers — swap `samples/lpadc` for `samples/lpcmp`,
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
without doing anything — a runtime phase that passes while exercising nothing. Every
case's runtime and system overlays therefore set
`CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE=y`.

A knock-on effect worth knowing when you write a new case: once runtime PM is genuinely
enabled, the device boots SUSPENDED, so any unconditional sanity read in a shared
baseline phase fails unless *something* takes a reference. A driver whose API path does
that itself — as the LPADC now does — needs no wrapping; for one that does not, the
baseline read has to be wrapped in `pm_device_runtime_get()`/`put()`, otherwise the
control check fails for exactly the reason the runtime phase is there to document.

## Watch out for: system-managed and runtime device PM are mutually exclusive

`pm_suspend_devices()` skips any device that is busy, is a wakeup source, or has runtime
PM enabled. So a build with both `CONFIG_PM_DEVICE_SYSTEM_MANAGED=y` and
`CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE=y` exercises neither path for the device under
test: the sweep passes it over and nothing else suspends it. The two are separate layers
(`overlay-pm-runtime.conf`, `overlay-pm-sysmanaged.conf`) for that reason.

A second trap in the same symbol: `PM_DEVICE_SYSTEM_MANAGED` is `default y if
!PM_DEVICE_RUNTIME` inside `if PM_DEVICE`, with no dependency on `PM`. So the plain
device-PM layer (`CONFIG_PM_DEVICE=y` and nothing else) also has it set, even though
there is no system PM and therefore no sweep at all. A `#if` that selects between the
manual phase and the sweep phase has to test `CONFIG_PM` as well, or the manual phase
compiles out of the very layer it belongs to and the build fails on an unused helper.

The same applies to power domains built on `power-domain-soc-state-change`: the domain
device gets its own `SUSPEND`/`RESUME` from the sweep, and that is what makes it hand its
children `TURN_OFF`/`TURN_ON`. Under runtime PM the domain is never swept, so its
children never see those actions.

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
6. Write `device-pm-case.json` naming the DT compatibles and driver sources the case
   covers. Without it the Device PM report cannot tell which IP the sample exercises, so
   the case is invisible there however green it is locally.
7. After a run, add a `results/<date>-<sample>-<what>.json` file. A case that exists but
   has never run is reported as exactly that, which is the honest state — do not leave a
   passing run unrecorded and do not edit an old file to describe a new run.

## What each case verifies

- `samples/lpadc/README.md` — five layers, covering both device-PM modes plus the power
  domain. The defects it found are all fixed in the driver now: a read against a
  SUSPENDED converter used to block forever, the read path took no runtime reference at
  all, `adc_channel_setup()` unbalanced the bandgap regulator, the driver did not even
  *compile* with `CONFIG_PM_DEVICE=y` and two enabled instances
  (`LPADC_PM_DEVICE_DEFINE` was not instance-parameterized), and nothing restored the
  register block after Deep Power Down. The README keeps each one with the reasoning, so
  a regression is recognisable.
- `samples/lpcmp/README.md` — two driver defects: the comparator boots with
  `CCR0.CMP_EN` set while PM reports SUSPENDED, and `set_trigger_callback()` re-enables a
  suspended comparator behind PM's back.
