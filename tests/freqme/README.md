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
  Route reads and deliberate writes enable the INPUTMUX clock with `INPUTMUX_Init()`
  and release it with `INPUTMUX_Deinit()`, as the mux driver does. On MCXA, reading
  the route with that clock gated causes a precise bus fault even after FREQME
  has been configured.
- **What the API reports**: a frequency within 5 % of 12 MHz for the default `FRO_12M`
  target, and WINDOW callbacks. A WINDOW monitor configured against twice the target's
  rate reports on every window, so whether callbacks arrive is a direct reading of
  whether the hardware is monitoring.

What is not observable: whether the peripheral clock gate is open, and anything in
the block before the first `configure()`. Until then the block can still be held in
peripheral reset, where a read faults — on MCX A it is, and a `CTRLSTAT` read there is
a precise bus fault — so the case reads FREQME registers only after configuring, and
the route only once the device has been turned on. That nothing is measuring at boot
is therefore asserted through the API, not the registers.

The main and ztest threads each have a 2048-byte stack for the configuration
reports, assertions and immediate logging. The default 1024-byte main stack
overflows during the runtime layer's suite setup on FRDM-MCXA153.

## What each layer claims

| Layer | Claim |
| --- | --- |
| `baseline` | a measurement is plausible, the route matches the devicetree, a configured block is distinguishable from reset, and WINDOW is silent on a good clock and reports on a bad one |
| `device` | `SUSPEND` stops a running monitor and clears the interrupt enables; `RESUME` re-arms it with no `start()`; a `MEASURE` started after a plain suspend/resume still delivers its callback; `TURN_ON` rebuilds a clobbered block and route and leaves it not measuring |
| `runtime` | a `configure()` without a reference is refused with `-EBUSY`; the route is applied once the first reference is held, even when the device booted OFF; the measured result stays readable while suspended; `MEASURE` and `WINDOW` both come back with the reference |
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

## Low-power timer prerequisites

The `sysmanaged` and `dpd` layers need a working system-timer wakeup path, not just
FREQME device PM. When an OS Timer stops or loses its state in the selected power
state, its `handoff-power-states` must include that state, and its idle-entry hook
must arm the low-power companion for the actual sleep deadline. Selecting a
companion alone does not schedule a wake alarm.

Every enabled device that loses registers must also follow the power-domain
lifecycle, including board-enabled UARTs that this case does not use. A UART
without its CORE-domain association receives only ordinary `RESUME` after DPD,
which can access an unclocked register instead of first restoring it through
`TURN_ON`. Keep those devices enabled and describe their actual domain; disabling
them or selecting a shallower state would not validate the same configuration.

The `dpd` layer additionally requires transparent SoC S2RAM resume. A wakeup reset
followed by a new boot banner is not a pass: execution must return to the existing
test, restore the configured monitor and route, replace the deliberately
overwritten `MIN`, and complete the post-transition measurement.

On MCXN, S2RAM must also preserve the consumer-enabled source outputs in
`SYSCON.CLOCK_CTRL`. Rebuilding the board clock tree does not restore the
`FRO12MHZ_ENA` and `FRO_HF_ENA` outputs used by FREQME. A valid clock-rate query,
restored route and asserted `MEASURE_IN_PROGRESS` do not prove that clock edges
reach the block. Restore the saved output enables before device resume, rather
than adding a post-wakeup source-clock enable to the test.

## Status

Results: [freqme.json](../../results/freqme.json).

| Board | baseline | device | runtime | system | sysmanaged | dpd |
| --- | --- | --- | --- | --- | --- | --- |
| FRDM-MCXA153 | pass | pass | pass | pass | pass | pass |
| FRDM-MCXA156 | pass | pass | pass | pass | pass | pass |
| FRDM-MCXA346 | pass | pass | pass | pass | pass | pass |
| FRDM-MCXA577 | pass | pass | pass | pass | pass | pass |
| FRDM-MCXN236 | pass | pass | pass | pass | pass | pass |
| FRDM-MCXN947 (CPU0) | pass | pass | pass | pass | pass | pass |

Coverage is limited to the six boards above; MCXA344 and the other declared
targets are not covered by this result. Tested revisions are recorded in the JSON.
