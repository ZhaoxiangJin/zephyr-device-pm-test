# LPADC device-PM test

Exercises the power-management hooks of `drivers/adc/adc_mcux_lpadc.c` across the
MCXN and MCXA families, through the six layers of
[../../docs/pm-layers.md](../../docs/pm-layers.md).

## What is observable

Whether a conversion completes. The driver's `SUSPEND` calls `LPADC_Enable(false)`,
so a completed conversion and a powered, configured, calibrated block are the same
fact — which is why this case needs no register peeking and no external wiring. The
absolute code is printed but never asserted on.

That makes LPADC the easy case. Most drivers cannot fail an API call on a suspended
device and have to assert on a register instead; see
[../../docs/authoring-a-case.md](../../docs/authoring-a-case.md).

## Boards

`boards/<board_target>.overlay` is applied automatically from the board target name.

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

- MCXN parts have an on-chip Vref regulator node, pinned to
  `NXP_VREF_MODE_HIGH_POWER`; that is why `prj.conf` sets `CONFIG_REGULATOR=y`. MCXA
  parts have no such node and use the external VREFH pin.
- `frdm_mcxa266`, `frdm_mcxa346`, `frdm_mcxa366` and `frdm_mcxa577` also need
  `CONFIG_LPADC_DO_OFFSET_CALIBRATION=y` from `boards/<board>.conf`; without it they
  return a meaningless code.
- Channel numbers match `samples/drivers/adc/adc_dt/boards/`, so they land on pins
  the board's `pinmux_lpadc0` already configures.
- Three of those boards enable `lpadc1` as well in `board_common.dtsi`; the overlays
  disable it so the layers drive exactly the one instance this case wires up.

## What each layer asserts

| Layer | Assertion |
| --- | --- |
| `baseline` | a conversion completes at all — the control, compiled into every layer |
| `device` | boots ACTIVE; `SUSPEND` → a read is refused with `-EBUSY`; `RESUME` → it works; a second `RESUME` is `-EALREADY` |
| `runtime` | boots SUSPENDED; an unwrapped read auto-resumes and drops back; a caller's own reference survives the driver's get/put; `adc_channel_setup()` on a suspended device changes nothing but is applied on the way up |
| `system` | no policy lock is held before a conversion and none is left behind after it |
| `sysmanaged` | the sweep's `SUSPEND`/`RESUME` round-trips across one forced Deep Sleep, which the LPADC survives |
| `dpd` | the same across Deep Power Down, which it does not: the block returns from reset and the driver's `TURN_ON` has to re-run clock setup, `LPADC_Init()` and calibration |

`dpd` is the layer with something to prove. Its `TURN_ON` enables the reference
regulator only for the length of the calibration and drops it again, so a device
left suspended afterwards does not strand the regulator enabled.

Two things in the `dpd` build exist only so the test can report its own result. The
console is on the core domain too but its driver has no restore hook, so
`pm_test_console_resume()` re-initialises the PORT / LP_FLEXCOMM / LPUART chain by
hand — that is exactly the workaround the LPADC no longer needs. And the retained
SRAM window is 24 KB, applied by the `pm-dpd` snippet on MCXN only.

## Follow-up

The `system` layer checks that the constraint is taken and released, not that it is
*honoured*: actually attempting Power Down mid-conversion and confirming it is
refused needs an idle-thread and residency setup this case does not have.

Defects this case found, open and fixed, are in
[../../docs/findings.md](../../docs/findings.md).
