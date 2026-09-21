# VREF device-PM test

Exercises the power-management hooks of the NXP VREF regulator driver
(`drivers/regulator/regulator_nxp_vref.c`) on the MCXN families.

A single `src/main.c` runs different phases depending on which PM Kconfigs are
enabled by the overlay you build with. Every phase prints `PM-TEST:` lines and the
run ends with `PM-TEST: RESULT PASS` or `PM-TEST: RESULT FAIL`.

## What is observable, and why

The VREF PM callback does exactly one thing:

```c
TURN_ON                     ->  configure_hw()  /* clock, CSR bits, trim */
RESUME / SUSPEND / TURN_OFF ->  0               /* nothing at all */
```

`configure_hw()` is the part of `init()` that writes hardware, lifted out so it can
run again: enable the clock if the node has one, `disable()` the output, set
`CSR[ICOMPEN]`, `CSR[CHOPEN]` and `CSR[REGEN]` according to the devicetree, then
either restore the trim a consumer asked for through `set_voltage()` or take the
trim to the bottom of its range.

So what this case observes is register content, read straight out of the block:

- the three CSR configuration bits, with the expectation derived from the same
  devicetree properties the driver reads (`nxp,current-compensation-en`,
  `nxp,chop-oscillator-en`, `nxp,internal-voltage-regulator-en`), so the check
  follows the board rather than restating one board's configuration
- `UTRIM`, through `regulator_get_voltage()` where the phase is asking about the
  consumer's choice and directly where it is asking about a register
- `CSR[VREFST]`, the bandgap-stable status bit, as the answer to "is there an
  output"
- the mode, through `regulator_get_mode()`, which reads back `CSR[HI_PWR_LV]` and
  `CSR[BUF21EN]`

There is no clock gate to worry about: the MCXN `vref` node carries no `clocks`
property, so the driver's clock branch is skipped and the block is always clocked.

### Standing in for the reset state

In the manual device-PM layer nothing has reset anything, so the phase clobbers
the registers by hand — clears the three CSR bits, writes a wrong trim code — and
then requires `TURN_ON` to put them back. The trim is written **directly to
`UTRIM`**, not through `regulator_set_voltage()`: `set_voltage()` latches
`data->trim_set`, and one of the two branches under test is the one that runs when
no consumer has asked for a voltage.

That branch is variant-dependent, and the sample splits the same way the driver
does:

| Variant | Trim field | Untrimmed `TURN_ON` behaviour |
| --- | --- | --- |
| `FSL_FEATURE_VREF_HAS_TRIM2V1 == 0` | `UTRIM[VREFTRIM]`, factory-loaded at reset | left alone |
| otherwise (both MCXN families) | `UTRIM[TRIM2V1]` | driven to the bottom of the range |

MCXN947, MCXN547 and MCXN236 do not define the feature at all, so they take the
second row: the check is that the trim reads 0 after `TURN_ON`. On a factory-trim
part the check is that the clobbered value survived, and the sample notes that it
has overwritten the factory trim for the rest of the run — nothing here depends on
absolute accuracy.

## The finding this test reports: the output is not re-enabled

`configure_hw()` opens with `regulator_nxp_vref_disable()`, which clears `HCBGEN`,
`LPBGEN` and `BUF21EN`. Nothing puts them back: `regulator-initial-mode` is applied
by `regulator_common_init()` at boot only, and the enable path belongs to
`regulator_enable()`, which the common layer calls when the reference count goes
from 0 to 1.

After a Deep Power Down the reference count is still whatever it was — it lives in
retained SRAM — so a consumer that enabled the output before the transition still
holds its reference, `regulator_is_enabled()` still returns true, and yet:

```
PM-TEST: CHECK FAIL - regulator-initial-mode restored after the transition
PM-TEST: CHECK FAIL - output stable after the transition for a held reference
```

The consumer gets no error and no callback; it just reads a converter against a
dead reference. **The `dpd.mcxn` scenario is expected to fail on both of these
checks**, and the scenario then demonstrates that the block itself is fine:

```
PM-TEST: CHECK PASS - output stable again after a disable/enable cycle
```

Whose job the re-enable is, is genuinely arguable:

- **the driver's**, by the same reasoning its own comment gives for the trim — a
  consumer's `set_voltage()` choice is restored because "nothing else puts it back
  once the block has been reset", and a consumer's `enable()` is in exactly that
  position. The reference count that says the output is wanted is already in the
  driver's data.
- **the consumer's**, because `SUSPEND`/`TURN_OFF` return 0 on the stated ground
  that "whether the output is on is owned by the consumers through the regulator
  reference count, not by the SoC power state" — and an owner that is told the
  block lost power can be expected to act.

Either way the current behaviour is silent, which is the part worth reporting. The
mode is the weaker half of the two: `regulator_set_mode()` stores nothing in
`data`, so the driver could only restore the devicetree's initial mode, not a
consumer's later choice.

The manual device-PM layer deliberately only **prints** this state instead of
checking it. Nothing lost power in that layer, so the question of who should
re-enable does not arise there, and the `device` scenario stays a clean statement
of what `TURN_ON` does restore.

## Boards

`boards/<board_target>.overlay` provides the `test-vref` alias the sample resolves,
sets `status = "okay"` (redundant on the boards whose dts already enables the
reference, repeated so the case does not depend on that) and pins the mode to
`NXP_VREF_MODE_HIGH_POWER`, matching `samples/drivers/adc/adc_dt`.

| Board target | Notes |
| --- | --- |
| `frdm_mcxn947/mcxn947/cpu0` | `vref` enabled by the board dtsi |
| `frdm_mcxn947/mcxn947/cpu0/qspi` | same, XIP variant |
| `frdm_mcxn236` | `vref` enabled by the board dts |
| `mcx_n9xx_evk/mcxn947/cpu0` | |
| `mcx_n9xx_evk/mcxn947/cpu0/qspi` | |
| `mcx_n5xx_evk/mcxn547/cpu0` | MCXN547, same `nxp,vref` block |

No MCXA target: no MCXA SoC has an `nxp,vref` node. The MCXC parts do have a
reference, but it is `nxp,vrefv1` and a different driver, so the sample refuses to
build against it with a `BUILD_ASSERT`-style `#error`.

## Phases & expectations

Substitute any board target from the table above for `$BOARD`.

### Baseline — `prj.conf` (no PM)
```sh
west build -b $BOARD . -p always
```
Control layer, and the one that sets up the premise for all the others:

- the device is ready
- the CSR carries the devicetree configuration bits at init. This holds in *every*
  build, including this one: with `CONFIG_PM_DEVICE=n`, `pm_device_driver_init()`
  is an inline stub that calls the action callback with `TURN_ON` and then
  `RESUME`, so `configure_hw()` has run either way.
- the mode matches `regulator-initial-mode`
- `regulator_enable()` succeeds and `CSR[VREFST]` reads stable. **The sample then
  keeps that reference for the rest of the run**, which is what makes every later
  phase a question about a consumer that needs the output.
- in every build except the manual device-PM one, it also picks a voltage out of
  the middle of the variant's range and asks for it, so `data->trim_set` is latched
  and the sweep phase has a consumer trim to check. That is skipped in the
  device-PM build because the untrimmed branch has to stay observable there.

`regulator_enable()` spins on `CSR[VREFST]` inside the driver, so a bandgap that
never stabilises hangs rather than failing. If a run stops after
`PM-TEST: CHECK PASS - vref device is ready`, that spin is where it is.

### Device PM — `overlay-pm-device.conf` (`CONFIG_PM_DEVICE`)
```sh
... -- -DEXTRA_CONF_FILE=overlay-pm-device.conf
```
Two sub-phases, each running the only sequence `pm_device_action_run()` accepts
from `ACTIVE` (it checks the current state against `action_expected_state[]`):
`SUSPEND` → `TURN_OFF` → `TURN_ON` → `RESUME`.

*No consumer trim:*
- boots **ACTIVE**
- registers clobbered by hand
- `SUSPEND` accepted, and **leaves the block alone**
- `TURN_OFF` accepted, and **leaves the block alone**
- `TURN_ON` restores the devicetree configuration bits, and takes the trim to the
  bottom of the range (or leaves the factory trim, per the table above)
- `RESUME` accepted, state `ACTIVE`

*With a consumer trim:*
- `set_voltage()` / `get_voltage()` roundtrip
- trim clobbered by hand
- `SUSPEND` → `TURN_OFF` → `TURN_ON`
- `get_voltage()` reads the consumer's choice again — the headline claim
- `RESUME`, `ACTIVE`

Then the mode and output state are printed, not checked, for the reason given
above.

### Runtime PM — `overlay-pm-runtime.conf` (`+ CONFIG_PM_DEVICE_RUNTIME`)
```sh
... -- -DEXTRA_CONF_FILE=overlay-pm-runtime.conf
```
- boots **SUSPENDED or OFF**, and the output is **stable anyway**. The regulator API
  takes no runtime PM reference of its own, so a consumer that enabled the output never
  asked PM for anything — and the driver's `SUSPEND` is a no-op precisely so that
  output keeps running.
- `runtime_get` → ACTIVE; `runtime_put` → SUSPENDED
- the output is still stable and the configuration bits are still set after the
  `put`

#### On silicon the configuration bits are *not* set — and that is the finding

The vref node names `&core_domain`. That domain device is runtime enabled too, so
`pm_device_runtime_auto_enable()` suspends it right after its own init, and by the time
`regulator_nxp_vref_init()` runs `pm_device_is_powered()` already answers false:
`pm_device_driver_init()` skips `TURN_ON`, so `regulator_nxp_vref_configure_hw()` never
runs and the device settles in OFF rather than SUSPENDED.

Nor does a later `pm_device_runtime_get()` repair it.
`power-domain-soc-state-change` forwards `TURN_ON` to its children only when the next
system power state is one of its `onoff-power-states`, and an ordinary resume is not one
of them — the device goes from OFF straight to ACTIVE on a bare `RESUME`.

So in this layer the CSR reads `0x00010800` at boot and `0x80010807` after a full
get/put cycle: `HI_PWR_LV` and `BUF21EN` are there because `regulator_common_init()`
writes the mode from `init`, but `ICOMPEN`, `CHOPEN` and `REGEN` — the three devicetree
properties — never arrive. Nothing reports an error: `regulator_enable()` succeeds,
`VREFST` comes up, a voltage can be set and read back. The reference simply runs
without the current compensation, chop oscillator and internal regulator the board asked
for. The case reports this as a failure in the baseline phase (`CSR carries the
devicetree configuration bits at init`) and again after the runtime cycle.

The PORT case hits the identical mechanism and shows no symptom, because
`pinctrl_mcux_init()` opens its clock gate itself instead of leaving it to `TURN_ON`.
That is the argument for doing hardware setup from `init` as well as from `TURN_ON`
whenever a device sits on this kind of domain.

### System PM + constraints — `overlay-pm-system.conf` + `constraints.overlay`
```sh
... -- -DEXTRA_CONF_FILE=overlay-pm-system.conf \
       -DEXTRA_DTC_OVERLAY_FILE=constraints.overlay
```
- `constraints.overlay` adds `zephyr,disabling-power-states = <&deeppowerdown>` to
  `&vref`. Only that state: the reference is in the CORE domain and Deep Sleep and
  Power Down leave its registers and bandgap alone — which is the point of a
  reference a converter can keep using in a low-power state — and
  `&deeppowerdown` is the one state the SoC dtsi already names in `core_domain`'s
  `onoff-power-states`.
- the driver never calls `pm_policy_device_power_lock_get()`, and should not: an
  enabled reference is not an operation in flight. The phase checks that nothing
  holds a policy lock on the reference's behalf.

### System-managed sweep — `overlay-pm-sysmanaged.conf`
```sh
... -- -DEXTRA_CONF_FILE=overlay-pm-sysmanaged.conf
```
One forced **Deep Sleep** transition with `pm_suspend_devices()`/
`pm_resume_devices()` around it. Deep Sleep does not reset the block, so every
check holds trivially — including the two that fail under Deep Power Down. That
contrast is what makes this scenario worth keeping: it separates "the sweep broke
something" from "the block lost power".

### Deep Power Down — `overlay-pm-sysmanaged.conf` + `dpd-mcxn.overlay`
```sh
... -- -DEXTRA_CONF_FILE=overlay-pm-sysmanaged.conf \
       -DEXTRA_DTC_OVERLAY_FILE=dpd-mcxn.overlay
```
The only layer that reaches the driver's `TURN_ON` for real. Enabling
`&deeppowerdown` arms the SoC's `core_domain`, which lists that state in
`onoff-power-states`; the sweep suspends the domain, the domain hands the vref a
`TURN_OFF`/`TURN_ON` pair.

`dpd-mcxn.overlay` also narrows `zephyr,sram` to RAMA1–RAMA3, the window the VBAT
RAM LDO keeps alive. The driver's `data` — and with it the trim the consumer asked
for — lives there, which is what makes the restore possible at all and why this
layer can assert on a voltage chosen before the transition.

The register state is sampled **before** the console is touched, so what is
reported is what the block looked like the moment the sweep handed it back; then
the PORT pin-mux, the LP_FLEXCOMM parent and the LPUART are re-initialised by hand
so the result can be printed. (The pin-mux step is redundant on a tree where the
pin-mux driver has its own `TURN_ON` — see `samples/port` — and is kept so this
case does not depend on that half of the work.)

Expected: configuration bits and consumer voltage restored, `ACTIVE`, reference
count intact — and the two documented failures on mode and output, followed by a
successful disable/enable recovery.

## Helper

```sh
scripts/run_vref.sh <baseline|device|runtime|system|sysmanaged|dpd> [-b BOARD] [--flash]
```

## Notes / follow-ups

- Absolute output accuracy is not measured. The sample asserts on register content
  and on `CSR[VREFST]`; proving the output really is at the trimmed voltage would
  need the LPADC, which is what `samples/lpadc` is for (and it uses this same
  reference).
- `NXP_VREF_MODE_LOW_POWER` and `NXP_VREF_MODE_STANDBY` are not exercised. The
  mode question this case asks is whether the configured mode survives, not
  whether each mode works.
- **A build with device PM but without `CONFIG_PM` faults at boot**, before the console
  exists, so the runtime layer captures zero bytes rather than a stack dump. The cause is
  in the power-domain driver, not in the regulator: `pd_pm_action()` in
  `drivers/power_domain/power_domain_soc_state_change.c` dereferences
  `pm_state_next_get()`, which is a `NULL`-returning stub when `CONFIG_PM=n`, and
  `CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE` reaches it at `PRE_KERNEL_1`. See
  `samples/port/README.md` for the full path and the verified one-line guard.
- Not covered: the `_ns`/TrustZone board variants, and the `nxp,vrefv1` driver on
  MCXC.

