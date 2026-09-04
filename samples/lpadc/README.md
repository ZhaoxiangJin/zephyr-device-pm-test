# LPADC device-PM test

Exercises the power-management hooks of the NXP LPADC driver
(`drivers/adc/adc_mcux_lpadc.c`) across the MCXN and MCXA families.

A single `src/main.c` runs different phases depending on which PM Kconfigs are
enabled by the overlay you build with. Every phase prints `PM-TEST:` lines and
the run ends with `PM-TEST: RESULT PASS` or `PM-TEST: RESULT FAIL`.

The read is a single-ended conversion on one LPADC channel. No external wiring is
needed: the test judges *whether a conversion completes*, not its absolute value —
that is what distinguishes an ACTIVE peripheral from a SUSPENDED (disabled) one.
`src/main.c` itself is board-agnostic; it drives whatever `zephyr,user
io-channels` points at.

## Boards

`boards/<board_target>.overlay` is applied automatically by Zephyr from the board
target name. The channel and reference differ by family:

| Board target | Channel | Reference |
| --- | --- | --- |
| `frdm_mcxn947/mcxn947/cpu0` | CH0A | on-chip Vref, 1.8 V, 12-bit |
| `frdm_mcxn947/mcxn947/cpu0/qspi` | CH0A | on-chip Vref, 1.8 V, 12-bit |
| `frdm_mcxn236` | CH0A | on-chip Vref, 1.8 V, 12-bit |
| `mcx_n9xx_evk/mcxn947/cpu0` | CH0A | on-chip Vref, 1.8 V, 12-bit |
| `mcx_n9xx_evk/mcxn947/cpu0/qspi` | CH0A | on-chip Vref, 1.8 V, 12-bit |
| `mcx_n5xx_evk/mcxn547/cpu0` | CH0A | on-chip Vref, 1.8 V, 12-bit |
| `frdm_mcxa153` | CH0A | VREFH pin, 3.3 V, 16-bit |
| `frdm_mcxa156` | CH0A | VREFH pin, 3.3 V, 16-bit |
| `frdm_mcxa266` | CH7A | VREFH pin, 3.3 V, 16-bit |
| `frdm_mcxa344` | CH7A | VREFH pin, 3.3 V, 16-bit |
| `frdm_mcxa346` | CH7A | VREFH pin, 3.3 V, 16-bit |
| `frdm_mcxa366` | CH7A | VREFH pin, 3.3 V, 16-bit |
| `frdm_mcxa577` | CH9A | VREFH pin, 3.3 V, 16-bit |

- MCXN parts have an on-chip Vref regulator node; the overlay pins it to
  `NXP_VREF_MODE_HIGH_POWER`, and `prj.conf` therefore sets `CONFIG_REGULATOR=y`.
  MCXA parts have no such node and reference the external VREFH pin instead.
- `frdm_mcxa266`, `frdm_mcxa346`, `frdm_mcxa366` and `frdm_mcxa577` additionally get
  `CONFIG_LPADC_DO_OFFSET_CALIBRATION=y` from `boards/<board>.conf`; without it those
  parts return a meaningless code.
- The channel numbers match `samples/drivers/adc/adc_dt/boards/` so they land on pins
  the board's `pinmux_lpadc0` already configures.

## Finding: the driver cannot build with `CONFIG_PM_DEVICE=y` and two instances

`drivers/adc/adc_mcux_lpadc.c` has:

```c
#define LPADC_PM_DEVICE_DEFINE  PM_DEVICE_DT_INST_DEFINE(n, mcux_lpadc_pm_callback);
#define LPADC_PM_DEVICE_GET     PM_DEVICE_DT_INST_GET(n)
```

Both are *object-like* macros, so the `n` in the replacement list is a literal `n` rather
than the parameter of the `LPADC_MCUX_INIT(n)` macro they expand inside — macro parameter
substitution happens before nested macro expansion. Every instance therefore emits the
same symbol:

```
error: redefinition of '__pm_device_dts_ord_DT_N_INST_n_nxp_lpc_lpadc_ORD'
```

With a single enabled instance this goes unnoticed, because `LPADC_PM_DEVICE_GET` has the
same bug and so refers to the same (misnamed) object. With two enabled instances the
build breaks outright. `boards/nxp/frdm_mcxaxx6/board_common.dtsi` enables both `lpadc0`
and `lpadc1`, so `frdm_mcxa266`, `frdm_mcxa346` and `frdm_mcxa366` hit this.

The fix is to parameterize both macros (`LPADC_PM_DEVICE_DEFINE(n)` /
`LPADC_PM_DEVICE_GET(n)`). Until then those three board overlays disable `lpadc1` so the
PM phases can run at all; the workaround and its rationale are in each overlay.

## Phases & expectations

Substitute any board target from the table for `$BOARD`.

### Baseline — `prj.conf` (no PM)
```
west build -b $BOARD . -p always
```
- `baseline read succeeds` — control; if this fails the sample/board is wrong,
  not the PM code.

### Device PM — `overlay-pm-device.conf` (`CONFIG_PM_DEVICE`)
```
... -- -DEXTRA_CONF_FILE=overlay-pm-device.conf
```
- Device inits **ACTIVE** (no runtime PM → `pm_device_driver_init` resumes it).
- `SUSPEND` action succeeds → state `SUSPENDED`. A read while suspended is
  logged (not asserted) to document driver behavior against a disabled block.
- `RESUME` action succeeds → state `ACTIVE`; read succeeds again.
- Double `RESUME` returns `-EALREADY`.

### Runtime PM — `overlay-pm-runtime.conf` (`+ CONFIG_PM_DEVICE_RUNTIME`)
```
... -- -DEXTRA_CONF_FILE=overlay-pm-runtime.conf
```
- Device inits **SUSPENDED** (runtime PM path in `pm_device_driver_init`).
- **Finding:** the LPADC read path does *not* call `pm_device_runtime_get/put`,
  so an unwrapped read does not auto-resume the device. The test logs this
  unwrapped read, then shows the correct pattern: `runtime_get` → read →
  `runtime_put`, with state going ACTIVE then back to SUSPENDED.
- **Caveat:** this overlay does not yet set `CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE=y`,
  so runtime PM is compiled in but not enabled for the device and the get/put calls are
  no-ops. See the top-level README section "runtime PM has to be *enabled*, not just
  compiled in".

### System PM + constraints — `overlay-pm-system.conf` + `constraints.overlay`
```
... -- -DEXTRA_CONF_FILE=overlay-pm-system.conf \
       -DEXTRA_DTC_OVERLAY_FILE=constraints.overlay
```
- `constraints.overlay` adds `zephyr,disabling-power-states = <&powerdown
  &deeppowerdown>` to `&lpadc0`, so the driver's per-conversion
  `pm_policy_device_power_lock` blocks those states while a read is in flight.
  All MCXN/MCXA SoCs use those same power-state labels, so the file is family-wide.
- Confirms conversions still complete with constraints compiled in.

## Notes / follow-ups
- Deeper verification of the constraint (actually attempting to enter powerdown
  during a conversion and confirming it is blocked) needs an idle-thread /
  residency setup; left as a next step.
- Use `EXTRA_DTC_OVERLAY_FILE` (additive) rather than `DTC_OVERLAY_FILE` for the system
  phase — setting `DTC_OVERLAY_FILE` would suppress the automatic `boards/` lookup.
