# LPADC device-PM test

Exercises the power-management hooks of the NXP LPADC driver
(`drivers/adc/adc_mcux_lpadc.c`) on `frdm_mcxn947/mcxn947/cpu0`.

A single `src/main.c` runs different phases depending on which PM Kconfigs are
enabled by the overlay you build with. Every phase prints `PM-TEST:` lines and
the run ends with `PM-TEST: RESULT PASS` or `PM-TEST: RESULT FAIL`.

The read is a single-ended conversion on LPADC CH0A against the internal 1.8 V
Vref (see `app.overlay`). No external wiring is needed: the test judges *whether
a conversion completes*, not its absolute value — that is what distinguishes an
ACTIVE peripheral from a SUSPENDED (disabled) one.

## Phases & expectations

### Baseline — `prj.conf` (no PM)
```
west build -b frdm_mcxn947/mcxn947/cpu0 . -p always
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

### System PM + constraints — `overlay-pm-system.conf` + `constraints.overlay`
```
... -- "-DEXTRA_CONF_FILE=overlay-pm-system.conf" \
       "-DDTC_OVERLAY_FILE=app.overlay;constraints.overlay"
```
- `constraints.overlay` adds `zephyr,disabling-power-states = <&powerdown
  &deeppowerdown>` to `&lpadc0`, so the driver's per-conversion
  `pm_policy_device_power_lock` blocks those states while a read is in flight.
- Confirms conversions still complete with constraints compiled in.

## Notes / follow-ups
- Deeper verification of the constraint (actually attempting to enter powerdown
  during a conversion and confirming it is blocked) needs an idle-thread /
  residency setup; left as a next step.
- `app.overlay` is applied automatically; the system phase must list it
  explicitly alongside `constraints.overlay` in `DTC_OVERLAY_FILE`.
