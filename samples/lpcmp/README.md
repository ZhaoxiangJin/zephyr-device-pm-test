# LPCMP device-PM test

Exercises the power-management hooks of the NXP LPCMP driver
(`drivers/comparator/comparator_nxp_lpcmp.c`) across the MCXN and MCXA families.

A single `src/main.c` runs different phases depending on which PM Kconfigs are enabled
by the overlay you build with. Every phase prints `PM-TEST:` lines and the run ends with
`PM-TEST: RESULT PASS` or `PM-TEST: RESULT FAIL`.

## What is observable, and why

The LPCMP PM callback does exactly one thing:

```c
RESUME  ->  CCR0 |=  LPCMP_CCR0_CMP_EN_MASK
SUSPEND ->  CCR0 &= ~LPCMP_CCR0_CMP_EN_MASK
```

Everything else (`TURN_ON`, `TURN_OFF`) returns `-ENOTSUP`, and there is no register
save/restore. Unlike the LPADC case, an API call against a suspended LPCMP does **not**
fail: `comparator_get_output()` just returns the `CSR.COUT` latch, so it yields a stale
value with no error. An API-level test alone therefore cannot tell a working PM hook
from a no-op.

So the test asserts on **`CCR0.CMP_EN` read straight out of the peripheral**. The address
comes from `DT_REG_ADDR()` rather than a literal, so the same source works on every
board: on MCXN947 cpu0 the node's `reg` is `0x51000` and the `soc/peripheral` parent
translates it to `0x50051000`, while the MCXA parts place LPCMP0 elsewhere entirely.
`CCR0` is at offset `0x8` on all of them. `main()` prints the resolved address at
startup. The state and the bit are printed together on every
transition:

```
PM-TEST: state(init) = SUSPENDED, CCR0.CMP_EN=1
                       ^^^^^^^^^                ^ hardware disagrees with PM
```

That is enough to catch a PM state machine that has drifted from the hardware, and it
needs no external wiring. The optional loopback layer adds the stronger claim that the
analog comparison actually stopped.

## Two driver defects this test currently reports

**1. The comparator boots powered up while PM says SUSPENDED.**
`nxp_lpcmp_init()` sets `CCR0 |= CMP_EN` and only then calls `pm_device_driver_init()`.
With runtime PM auto-enabled, `pm_device_driver_init()` sets `state = SUSPENDED` and
returns early without ever running the SUSPEND callback, so the block is left enabled,
drawing analog current that PM believes it has already saved. Fails:

```
PM-TEST: CHECK FAIL - CMP_EN cleared at init to match SUSPENDED state
```

**2. `set_trigger_callback()` resurrects a suspended comparator.**
`nxp_lpcmp_set_trigger_callback()` clears `CMP_EN`, updates the callback, then
unconditionally sets `CMP_EN` again with no reference to PM state. Calling it on a
suspended device re-enables the hardware behind PM's back. Fails:

```
PM-TEST: CHECK FAIL - set_trigger_callback does not re-enable a SUSPENDED comparator
```

Both are asserted rather than merely logged, so **the `device`, `runtime` and `system`
layers report `RESULT FAIL` today**. That is the point of the harness: a driver fix shows
up as those runs turning into `RESULT PASS`. Only `baseline` should pass as-is.

A third, non-PM observation: the driver never calls `pinctrl_apply_state()` even though
its binding includes `pinctrl-device.yaml`, so `pinctrl-0` in the board overlay has no
effect. In particular the `bias-pull-up` in `&pinmux_lpcmp0` is not applied, which is
why no layer asserts on the comparator's output *value* unless the loopback layer is on.

## Boards

`boards/<board_target>.overlay` is applied automatically by Zephyr from the board target
name. Each one sets `&lpcmp0` to `okay` (no board dts enables it by default), selects the
positive mux input, and names the GPIO used by the optional loopback layer. The values
are copied from `tests/drivers/comparator/gpio_loopback/boards/`, so the wiring is
already documented and validated.

| Board target | Positive input | DAC | Loopback GPIO |
| --- | --- | --- | --- |
| `frdm_mcxn947/mcxn947/cpu0` (+ `/qspi`) | IN0, J2-17 | 127 of VREFH1 | `gpio1` 12, J2-11 |
| `frdm_mcxn236` | IN0, J2-8 | 127 of VREFH1 | `gpio1` 2, J2-10 |
| `mcx_n9xx_evk/mcxn947/cpu0` (+ `/qspi`) | IN0, J2-17 | 127 of VREFH1 | `gpio1` 1, J2-15 |
| `mcx_n5xx_evk/mcxn547/cpu0` | IN0, J2-17 | 127 of VREFH1 | `gpio1` 1, J2-15 |
| `frdm_mcxa153` | IN0, J2-9 | 127 of VREFH1 | `gpio1` 5, J2-3 |
| `frdm_mcxa156` | IN0, J2-9 | 127 of VREFH1 | `gpio1` 4, J2-1 |
| `frdm_mcxa266` | IN1, J2-17 | 127 of VREFH1 | `gpio1` 4, J2-7 |
| `frdm_mcxa344` | IN1, J2-17 | 127 of VREFH1 | `gpio1` 4, J2-7 |
| `frdm_mcxa346` | IN1, J2-17 | 127 of VREFH1 | `gpio1` 4, J2-7 |
| `frdm_mcxa366` | IN1, J2-17 | 127 of VREFH1 | `gpio1` 4, J2-7 |
| `frdm_mcxa577` | IN2, P1_4 (R132) | 160 of VREFH0 | `gpio0` 19, P0_19 (J6-1) |

## Phases & expectations

Substitute any board target from the table for `$BOARD`.

### Baseline — `prj.conf` (no PM)
```sh
west build -b $BOARD . -p always
```
Control layer. `get_output()` returns a valid level, `set_trigger()` /
`trigger_is_pending()` work, `CMP_EN` is set. If this fails, the sample or board setup
is wrong, not the PM code.

### Device PM — `overlay-pm-device.conf` (`CONFIG_PM_DEVICE`)
```sh
... -- -DEXTRA_CONF_FILE=overlay-pm-device.conf
```
- device boots **ACTIVE** with `CMP_EN=1` (no runtime PM → `pm_device_driver_init()`
  resumes it)
- `SUSPEND` → state `SUSPENDED` **and** `CMP_EN=0`
- `set_trigger_callback()` while suspended must not re-enable it — **defect 2, fails**
- `RESUME` → state `ACTIVE` and `CMP_EN=1`
- `CCR2` (input mux / hysteresis / power mode) is unchanged across the round trip: the
  driver has no save/restore, and does not need one, because the block stays powered
- double `RESUME` → `-EALREADY`
- `TURN_OFF` → logged, `-ENOTSUP` expected

### Runtime PM — `overlay-pm-runtime.conf` (`+ CONFIG_PM_DEVICE_RUNTIME`)
```sh
... -- -DEXTRA_CONF_FILE=overlay-pm-runtime.conf
```
- device boots **SUSPENDED**, and `CMP_EN` should be 0 to match — **defect 1, fails**
- an unwrapped `get_output()` is logged, not asserted: the driver takes no runtime
  reference, so it neither resumes the device nor errors
- `runtime_get` → ACTIVE, `CMP_EN=1`; `runtime_put` → SUSPENDED, `CMP_EN=0`
- nested `get`/`get`/`put`/`put`: stays ACTIVE across the first `put`, suspends only on
  the last one

> This overlay also sets `CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE=y`, which is
> **required**, not cosmetic. With `CONFIG_PM_DEVICE_RUNTIME=y` alone,
> `pm_device_driver_init()` resumes the device to ACTIVE and leaves runtime PM
> *disabled* for it; `pm_device_runtime_get()`/`put()` then return 0 without doing
> anything, and the whole phase would pass while testing nothing. The alternative is
> `zephyr,pm-device-runtime-auto` on the DT node.

### System PM + constraints — `overlay-pm-system.conf` + `constraints.overlay`
```sh
... -- -DEXTRA_CONF_FILE=overlay-pm-system.conf \
       -DEXTRA_DTC_OVERLAY_FILE=constraints.overlay
```
- `constraints.overlay` adds `zephyr,disabling-power-states = <&deepsleep &powerdown
  &deeppowerdown>` to `&lpcmp0`. Rationale: the NXP board files enable the CMP0 analog
  block for active mode only (`SPC_EnableActiveModeAnalogModules()`, never
  `SPC_EnableLowPowerModeAnalogModules()`), so the comparator loses its bias from deep
  sleep downwards. Plain `sleep` only gates the core clock, so it is not listed. All
  MCXN/MCXA SoCs declare these same four power-state labels, so one file covers the
  whole family.
- unlike the LPADC driver, the LPCMP driver never calls
  `pm_policy_device_power_lock_get()` itself, so the application has to hold the lock
  for as long as it needs the comparator output. The phase prints this.
- includes the runtime phase, so it inherits defect 1's failure.

### Optional GPIO loopback (`CONFIG_PM_TEST_LPCMP_LOOPBACK`)

Requires a jumper wire between the loopback GPIO and the positive input listed in the
board table above — same wiring as `tests/drivers/comparator/gpio_loopback`:

```
FRDM-MCXN947   J2-11 (PIO1_12, gpio1.12)  ---->  J2-17 (PIO1_0, CMP0_IN0)
FRDM-MCXA153   J2-3  (gpio1.5)            ---->  J2-9  (CMP0_IN0)
```

The exact two pins for any supported board are named in the header comment of
`boards/<board_target>.overlay`.

Stack it on any PM layer:

```sh
... -- "-DEXTRA_CONF_FILE=overlay-pm-device.conf;overlay-loopback.conf"
```

This is the only layer that asserts on the real analog result. It drives the positive
input high then low and requires that `comparator_get_output()` tracks both edges while
ACTIVE, and does **not** track them while SUSPENDED — proving the comparison stopped,
not just that a register bit changed. If `gpio1` is not ready the run fails immediately
with a wiring message, so a missing jumper cannot be mistaken for a driver defect.

## Helper

```sh
scripts/run_lpcmp.sh <baseline|device|runtime|system> [-b BOARD] [--loopback] [--flash]
```

## Notes / follow-ups

- Deeper verification of the constraint (actually entering powerdown and confirming it
  is blocked) needs an idle-thread / residency setup; left as a next step, same as the
  LPADC case.
- The driver's `enable-stop-mode` property plus a `FRO_16K`/`XTAL32K` function clock
  would keep the comparator running in stop modes, which would change what belongs in
  `zephyr,disabling-power-states`. Not exercised here.
- Use `EXTRA_DTC_OVERLAY_FILE` (additive) rather than `DTC_OVERLAY_FILE` for the system
  layer — setting `DTC_OVERLAY_FILE` would suppress the automatic `boards/` lookup.
- Not covered: the `cpu1` clusters and the `_ns`/TrustZone board variants.
