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

## Finding (fixed): the driver could not build with `CONFIG_PM_DEVICE=y` and two instances

`drivers/adc/adc_mcux_lpadc.c` had:

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
`LPADC_PM_DEVICE_GET(n)`), which is now done in the driver — `frdm_mcxa266` builds both
instances with `CONFIG_PM_DEVICE=y` and emits two distinct `__pm_device_dts_ord_*`
objects. The `lpadc1` disable in those three board overlays is therefore no longer
required to work around this; it is kept only so the PM phases drive exactly the one
instance the test wires up.

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
- `SUSPEND` action succeeds → state `SUSPENDED`. A read while suspended must
  fail with `-EBUSY`: the driver rejects it up front instead of arming a
  conversion on a disabled block.
> **Finding (fixed):** the driver used to accept that read, `LPADC_Enable(false)`
> meant the completion interrupt never arrived, and `adc_context`'s default
> `K_FOREVER` wait blocked the caller permanently. The driver now checks the PM
> state before taking the ADC context lock, and bounds the completion wait with
> `CONFIG_ADC_MCUX_LPADC_ACQUISITION_TIMEOUT_MS` (`-EAGAIN` plus a sequence
> abort on expiry) so no other route can hang either.
- `RESUME` action succeeds → state `ACTIVE`; read succeeds again.
- Double `RESUME` returns `-EALREADY`.

### Runtime PM — `overlay-pm-runtime.conf` (`+ CONFIG_PM_DEVICE_RUNTIME`)
```
... -- -DEXTRA_CONF_FILE=overlay-pm-runtime.conf
```
- Device inits **SUSPENDED** (runtime PM path in `pm_device_driver_init`).
- An *unwrapped* read auto-resumes the converter, completes, and lets it go
  again: the driver takes a runtime reference for the length of the sequence and
  drops it from the watermark interrupt. The state must be back to `SUSPENDED`
  shortly after the read returns.
- A caller that holds its own `pm_device_runtime_get()` across several reads must
  keep the device ACTIVE — the driver's own get/put pair must not disturb that
  reference.
- `adc_channel_setup()` on a SUSPENDED device returns 0 and leaves it SUSPENDED.
  It only records what `RESUME` has to apply; touching the reference supplies
  from there would raise the regulator's use count with nothing left to lower it.
> **Finding (fixed):** the read path used to take no reference at all, so an
> unwrapped read ran against a disabled converter. The driver now wraps each
> sequence in `pm_device_runtime_get()` / `pm_device_runtime_put_async()` plus
> `pm_device_busy_set()`/`_clear()`. The async put is why
> `CONFIG_ADC_MCUX_LPADC` selects `CONFIG_PM_DEVICE_RUNTIME_ASYNC`: the
> reference is dropped from interrupt context, which the synchronous put cannot
> do.
>
> The suspend itself is therefore deferred to the system work queue, so the test
> sleeps briefly before sampling the state.
> This overlay also sets `CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE=y`, which is
> **required**, not cosmetic. With `CONFIG_PM_DEVICE_RUNTIME=y` alone,
> `pm_device_driver_init()` resumes the device to ACTIVE and leaves runtime PM *disabled*
> for it; `pm_device_runtime_get()`/`put()` then return 0 without doing anything, and the
> whole phase would pass while testing nothing. The alternative is
> `zephyr,pm-device-runtime-auto` on the DT node.

### System PM + constraints — `overlay-pm-system.conf` + `constraints.overlay`
```
... -- -DEXTRA_CONF_FILE=overlay-pm-system.conf \
       -DEXTRA_DTC_OVERLAY_FILE=constraints.overlay
```
- `constraints.overlay` sets `zephyr,disabling-power-states = <&deepsleep
  &powerdown &deeppowerdown>` on `&lpadc0`, so the driver's per-conversion
  `pm_policy_device_power_lock` blocks those states while a read is in flight.
  All MCXN/MCXA SoCs use those same power-state labels, so the file is family-wide.
  Every MCXN/MCXA SoC DTS now declares the same list, so on those SoCs the overlay
  only restates it; it is kept because this phase exercises the constraint
  mechanism, which has to work on a board whose SoC DTS predates that list.
- The list follows MCX N23x RM Rev 4 Table 220, and the equivalent table in every
  other MCXN/MCXA RM says the same thing: the ADC analog block is not active in
  Power Down, and Deep Power Down gates the whole CORE domain including the
  registers. Deep Sleep is included conservatively — the converter *can* keep
  running there (`CTRL[DOZEN]` = 0 by reset, hardware trigger and compare wakeup
  stay live, only the bus clock is gated) but only while ADCK survives, which the
  default clock configuration does not promise. A project that arranges a
  low-power ADCK source can drop `&deepsleep` in its own overlay.
- Confirms conversions still complete with constraints compiled in.

### System-managed sweep — `overlay-pm-sysmanaged.conf` (`CONFIG_PM_DEVICE_SYSTEM_MANAGED`)
```
... -- -DEXTRA_CONF_FILE=overlay-pm-sysmanaged.conf
```
This is the other half of `CONFIG_PM_DEVICE`: instead of the driver managing its
own references, `pm_suspend_devices()` sweeps every device before the SoC enters a
low-power state and `pm_resume_devices()` walks them back afterwards. It is
mutually exclusive with `CONFIG_PM_DEVICE_RUNTIME` — the sweep skips any device
that has runtime PM enabled, so a build with both tests nothing here.

- Device inits **ACTIVE** (no runtime PM), and a read succeeds.
- The phase then forces exactly one transition with `pm_state_force()` and a
  `k_sleep()`. Forcing it keeps the run deterministic and leaves the board awake
  — and therefore debuggable — for the rest of the test.
- After the transition the device must be **ACTIVE** again and a read must
  succeed.

Without a `dpd-*.overlay` the forced state is Deep Sleep, which the LPADC
survives; this variant checks that the sweep's `SUSPEND`/`RESUME` pair round-trips
cleanly. The wake-up needs no extra setup: every MCXA/MCXN board already sets
`/chosen/zephyr,system-timer-companion = &lptmr0`, so `SysTick` hands over to
LPTMR0 in low-power states and a plain `k_sleep()` is enough. LPTMR0 also carries
`wakeup-source` and `wakeup-ctrls = <&wuu ...>` in the SoC DTS, and the counter
driver arms the WUU route for the companion timer by itself, so the same
`k_sleep()` also wakes the SoC from Deep Power Down. The overlay only has to turn
on `CONFIG_COUNTER`, `CONFIG_COUNTER_MCUX_LPTMR_ALARM` and `CONFIG_WUC`.

> `pm_device_busy_set()` is what keeps the sweep from suspending the converter
> mid-sequence: `pm_suspend_devices()` skips busy devices. The driver sets it
> alongside the runtime reference, so it is in place in both device-PM modes.

### Deep Power Down — `+ dpd-mcx{n,a}.overlay`
```
... -- -DEXTRA_CONF_FILE=overlay-pm-sysmanaged.conf \
       -DEXTRA_DTC_OVERLAY_FILE=dpd-mcxn.overlay     # or dpd-mcxa.overlay
```
Deep Power Down gates the whole CORE domain, so the LPADC register block comes
back from reset — `SUSPEND`/`RESUME` alone cannot restore it, the driver has to
re-run `LPADC_Init()` and the offset calibration. This layer stacks on the
system-managed one (the domain is driven by the device sweep) and adds nothing to
its Kconfig: `CONFIG_PM_S2RAM` has no prompt, it is derived from whether a
suspend-to-ram power state is enabled in the devicetree. So the devicetree
overlay enabling `&deeppowerdown` is the whole layer.

- `TURN_OFF` / `TURN_ON` are delivered by the `peripheral_domain` node now
  declared in every MCXN/MCXA SoC DTS, with the LPADC nodes pointing at it via
  `power-domains`. That node uses `power-domain-soc-state-change` with
  `onoff-power-states = <&deeppowerdown>`, and `PM_STATE_FROM_DT` skips power
  states that are not `okay` — so the domain is completely inert until an
  application enables Deep Power Down, which is why it can live in the SoC DTS.
  `CONFIG_POWER_DOMAIN` is turned on by the SoC `Kconfig.defconfig` whenever
  `CONFIG_PM_DEVICE=y`.
- The driver's `TURN_ON` re-runs clock setup, `LPADC_Init()`, calibration and the
  interrupt wiring; it enables the reference regulator only for the duration of
  the calibration and drops it again, so a device that stays suspended afterwards
  does not leave the regulator enabled.
- The two overlays differ only in SRAM: on MCXN only SRAMA is retained across
  Deep Power Down, so `dpd-mcxn.overlay` narrows `&sram0` to a 24 KB window clear
  of the boot ROM scratch area (matching `samples/boards/nxp/mcxn_a/s2ram`). All
  MCXA SRAM is retained, so `dpd-mcxa.overlay` only enables the state.
- The console is *not* on the peripheral domain and its driver has no equivalent
  hook, so the test re-initialises the PORT / LP_FLEXCOMM / LPUART chain by hand
  after the wake purely so it can report its result. That workaround is exactly
  what the LPADC no longer needs.

## Notes / follow-ups
- `main()` ends in a `k_busy_wait()` spin instead of returning. Returning lets the
  idle thread park the core in WFI, which powers down the DAP: SWD access is lost
  and the next flash fails (`Failed to power up DAP`, or the ROM dropping into its
  ISP command loop). With `CONFIG_PM` it would also let the PM subsystem enter a
  low-power state behind the test's back.
- Because that spin never yields, `prj.conf` sets `CONFIG_LOG_MODE_IMMEDIATE=y`.
  In the default deferred mode the log thread never gets to run, and since
  `CONFIG_LOG_PRINTK` routes `printk` through it too, the entire run is swallowed
  — a capture comes back empty or truncated mid-line *even when the test passes*.
  That failure mode looks exactly like a hang, so do not remove this.
- Deeper verification of the constraint (actually attempting to enter powerdown
  during a conversion and confirming it is blocked) needs an idle-thread /
  residency setup; left as a next step.
- Use `EXTRA_DTC_OVERLAY_FILE` (additive) rather than `DTC_OVERLAY_FILE` for the system
  phase — setting `DTC_OVERLAY_FILE` would suppress the automatic `boards/` lookup.
