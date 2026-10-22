# FREQME

The NXP Frequency Measurement block, through the `clock_monitor` API
(`drivers/clock_monitor/clock_monitor_nxp_freqme.c`), on every MCXN and MCXA board target
that can enable `freqme0`.

## What is observable

Three things, all readable whether or not the device is suspended: on these parts a
FREQME register read with the peripheral gate closed returns the retained contents and
does not fault.

- **`CTRLSTAT`**, which mirrors every field of the write-only `CTRL_W`. Whether the
  block is measuring is `MEASURE_IN_PROGRESS` and `CONTINUOUS_MODE_EN`; whether the
  consumer's configuration is in the block is `REF_SCALE`, the mode bits and the three
  interrupt enables, together with `MIN` and `MAX`.
- **The INPUTMUX route**, `FREQMEAS_REF` and `FREQMEAS_TAR`. It lives in another
  peripheral, so a block that measured correctly against the wrong clocks would pass
  every FREQME check. Its expected value comes from the devicetree connection cookie.
- **What the API reports**: a frequency within 5 % of 12 MHz for the default `FRO_12M`
  target, and WINDOW callbacks. A WINDOW monitor configured against twice the target's
  rate reports on every window, so whether callbacks arrive is a direct reading of
  whether the hardware is monitoring.

What is not observable: whether the peripheral clock gate is open. The case does not
need it — the driver never closes it — and a register read does not depend on it.

## What each layer claims

| Layer | Claim |
| --- | --- |
| `baseline` | a measurement is plausible, the route matches the devicetree, a configured block is distinguishable from reset, and WINDOW is silent on a good clock and reports on a bad one |
| `device` | `SUSPEND` stops a running monitor and clears the interrupt enables; `RESUME` re-arms it with no `start()`; a `MEASURE` started after a plain suspend/resume still delivers its callback; `TURN_ON` rebuilds a clobbered block and route and leaves it not measuring |
| `runtime` | the device boots with nothing measuring; a `configure()` without a reference is refused with `-EBUSY`; the route is applied once the first reference is held, even when the device booted OFF; the measured result stays readable while suspended; `MEASURE` and `WINDOW` both come back with the reference |
| `system` | the power-state lock covers exactly the declared states while measuring, is released by the interrupt that completes a `MEASURE`, and follows `WINDOW` monitoring across `stop()`, a suspend and a resume |
| `sysmanaged` | across Deep Sleep the block keeps its registers, the selected route survives and monitoring is re-armed |
| `dpd` | across Deep Power Down the block loses its registers and `TURN_ON` rebuilds them and the selected route, and monitoring is re-armed |

## How `sysmanaged` and `dpd` are told apart

Both switch to the `FRO_12M` reference first, so the route that has to come back is the
consumer's choice and not the boot default. Then `MIN` is overwritten behind the driver's
back with a value the driver does not hold in RAM. Across Deep Sleep nothing rewrites it,
so it has to survive. Across Deep Power Down `TURN_ON` rebuilds `MIN` from what
`configure()` applied, so the overwritten value has to be gone. That one register shows
both that power was really cut and that the replay ran.

## Boards

`frdm_mcxn947` and `frdm_mcxa346` do not enable `freqme0` or `inputmux0` in their own
devicetree; `boards/` enables both. Every other target in `platform_allow` enables them
itself. The case turns on the clocks of every declared source with a `clocks` property
before it starts, because the driver only queries them.

## Status

Built and run status is in [../../results/](../../results/). A case that has never run
is reported as exactly that.
