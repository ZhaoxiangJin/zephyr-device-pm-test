# PORT pin-mux device-PM test

Exercises the power-management hooks of the NXP PORT pin-mux driver
(`drivers/pinctrl/pinctrl_nxp_port.c`) across the MCXN and MCXA families.

A single `src/main.c` runs different phases depending on which PM Kconfigs are enabled
by the overlay you build with. Every phase prints `PM-TEST:` lines and the run ends with
`PM-TEST: RESULT PASS` or `PM-TEST: RESULT FAIL`.

## What is observable, and why

The PORT PM callback does exactly one thing:

```c
TURN_ON                     ->  clock_control_on(<syscon>, MCUX_PORTn_CLK)
RESUME / SUSPEND / TURN_OFF ->  0        /* nothing at all */
```

There is no register save/restore, and no API call to fail: nothing in the pinctrl API
takes a device, so "the pin-mux is suspended" is not observable through any function
call. The one hardware effect is the SoC's pin-mux clock gate, so that bit is what this
case asserts on.

It is read out of the **clock controller**, not out of the PORT block:

- MCXN — `SYSCON->AHBCLKCTRL0..3`, which are contiguous from offset `0x200` and are the
  readable side of the write-only `AHBCLKCTRLSET[]`/`AHBCLKCTRLCLR[]` aliases the HAL
  writes. The register index and bit come from `CLK_GATE_ABSTRACT_REG_OFFSET()` /
  `CLK_GATE_ABSTRACT_BITS_SHIFT()`.
- MCXA — `MRCC0->MRCC_GLB_CCn`, group stride `0x10`, which is what
  `CLK_GATE_REG_OFFSET()` / `CLK_GATE_BIT_SHIFT()` encode.

The PCR registers are never touched. They only answer while the block is clocked, so
reading one to find out whether the clock is on is precisely the wrong instrument: the
answer would arrive as a bus fault. Keeping the sample away from the PORT block also
means it never has to care that `PORT_PCR_COUNT` is 32 while not every index is a real
pin.

Two things follow from the driver's shape and are checked as such:

1. **`pinctrl_mcux_init()` opens the gate itself**, before handing over to
   `pm_device_driver_init()`. It has to: every driver that applies a pin state from its
   own init writes through these registers, and a device that sits in a power domain is
   only handed `TURN_ON` once something resumes the domain — which, for a domain that
   only tracks SoC power states, may be much later or never. So the gate must be open in
   *every* layer, including the runtime layer where PM reports the pin-mux as
   `SUSPENDED`.
2. **`SUSPEND` and `TURN_OFF` must leave the gate alone.** The pad configuration is
   state, not activity; gating it to "save power" would fault the next driver that
   re-applies its pin state.

### Standing in for the reset state

Nothing in a running build closes a pin-mux gate, and `clock_control_off()` will not do
it either — `clock_control_mcux_syscon.c` has no PORT case in its `.off` handler at all,
it just returns 0. So the device layer closes the gate with the HAL's
`CLOCK_DisableClock()` to stand in for the state the block comes back in after Deep
Power Down, then requires `TURN_ON` to re-open it.

The window between the two is a couple of register writes with nothing printed inside
it, because the console's own pin-mux may be the instance being gated. The PCR contents
are held by the block and not by the gate, so the pads keep their function while it is
closed — and if that turned out to be wrong, the garbled character would itself be the
finding. Whatever the driver does, the phase re-opens by hand any gate it finds still
closed, so one failure does not poison the rest of the run.

## The defect this test reports: PORT5 is never re-opened

`clock_control_mcux_syscon.c` handles `MCUX_PORT0_CLK` … `MCUX_PORT4_CLK` and stops
there, in both the MCXA/MCXL branch and the MCXN branch. Every SoC dtsi that has a
`portf` gives it `clocks = <&syscon MCUX_PORT5_CLK>`, which the clock driver silently
ignores — `clock_control_on()` returns 0 having done nothing.

For the driver's `TURN_ON` that means PORT5 is the one instance whose gate is not
re-opened after a power loss:

```
PM-TEST: CHECK FAIL - pinmux@e3000 (PORT5) clock gate open after the transition
```

- **MCXA577** has `kCLOCK_GatePORT5` (`FSL_FEATURE_SOC_PORT_COUNT` is 6), so the gate is
  real, observable, and stays closed. **The `dpd.mcxa` scenario is expected to fail on
  `frdm_mcxa577`** until the clock driver grows a `MCUX_PORT5_CLK` case.
- **MCXN947 / MCXN547 / MCXN236** declare `FSL_FEATURE_SOC_PORT_COUNT` as 6 as well, but
  the HAL defines `kCLOCK_Port0..Port4` only. Their `portf` also sits at `0x42000`,
  outside the `0x116000`–`0x11a000` window the other five share, so it is not on the same
  clock. There is nothing to assert, and the case says so rather than passing quietly:

  ```
  PM-TEST: pinmux@42000 (PORT5) has no observable gate on this SoC
  ```

Everything else — porta…porte on both families — is expected to pass.

## Boards

This case has no `boards/` directory. The `nxp,port-pinmux` nodes are enabled by the SoC
dtsi on every supported board and need no wiring, so `src/main.c` walks whatever is
enabled with `DT_FOREACH_STATUS_OKAY(nxp_port_pinmux, ...)` and derives each instance
number from `DT_CLOCKS_CELL(node, name) - MCUX_PORT0_CLK`. A `BUILD_ASSERT` per node
requires the clock controller to be `nxp,lpc-syscon`, which is what limits the case to
MCXN and MCXA: the Kinetis, MCXC, MCXE and MCXL parts wire the same driver to a
`sim`/`pcc`/root-clock controller with a different cell layout.

| Family | Instances found | Observable gates |
| --- | --- | --- |
| MCXN947 / MCXN547 / MCXN236 | porta…portf (6) | PORT0…PORT4 |
| MCXA153 | porta…portd (4) | PORT0…PORT3 |
| MCXA156 / MCXA266 / MCXA344 / MCXA346 / MCXA366 | porta…porte (5) | PORT0…PORT4 |
| MCXA577 | porta…portf (6) | PORT0…PORT5 |

## Phases & expectations

Substitute any board target from the table above for `$BOARD`.

### Baseline — `prj.conf` (no PM)
```sh
west build -b $BOARD . -p always
```
Control layer. Every instance is ready and its gate is open. If this fails, the sample or
the board is wrong, not the PM code.

### Device PM — `overlay-pm-device.conf` (`CONFIG_PM_DEVICE`)
```sh
... -- -DEXTRA_CONF_FILE=overlay-pm-device.conf
```
Per instance, in this order — the only sequence `pm_device_action_run()` accepts from
`ACTIVE`, since it checks the current state against `action_expected_state[]`:

- boots **ACTIVE** (no runtime PM → `pm_device_driver_init()` resumes it)
- `SUSPEND` → accepted, gate **still open**
- `TURN_OFF` → accepted, gate **still open**
- gate closed by hand → reads closed
- `TURN_ON` → gate **open again** (the claim this whole case exists for)
- `RESUME` → accepted, state `ACTIVE`

### Runtime PM — `overlay-pm-runtime.conf` (`+ CONFIG_PM_DEVICE_RUNTIME`)
```sh
... -- -DEXTRA_CONF_FILE=overlay-pm-runtime.conf
```
- boots **SUSPENDED or OFF**, and the gate is **open anyway** — the interesting claim of
  this layer, and the reason `pinctrl_mcux_init()` cannot leave the clock to `TURN_ON`.
  On these SoCs it is always OFF, because the pin-mux nodes name `&core_domain`: the
  domain device is runtime enabled too, `pm_device_runtime_auto_enable()` suspends it
  right after its own init, and by the time a pin-mux initialises at `PRE_KERNEL_1`
  `pm_device_is_powered()` already answers false, so `pm_device_driver_init()` skips
  `TURN_ON`. It does not arrive later either — see below.
- `runtime_get` → ACTIVE; `runtime_put` → SUSPENDED
- the gate is still open after the `put`: runtime suspend of a pin-mux must not gate it

#### `TURN_ON` never runs in this layer

`power-domain-soc-state-change` forwards `TURN_ON` to its children only when the *next
system power state* is one of its `onoff-power-states`, and an ordinary runtime resume is
not one of them. So a child that boots OFF goes straight to ACTIVE on a bare `RESUME` and
never sees `TURN_ON` at all, for the life of the application.

For the pin-mux the register view is unaffected — the gate is open because
`pinctrl_mcux_init()` opened it, which is exactly the argument for that call. A driver
that puts its hardware configuration *only* in `TURN_ON` is not so lucky; the VREF case
next door shows the same mechanism with a visible cost.

### System PM + constraints — `overlay-pm-system.conf` + `constraints.overlay`
```sh
... -- -DEXTRA_CONF_FILE=overlay-pm-system.conf \
       -DEXTRA_DTC_OVERLAY_FILE=constraints.overlay
```
- `constraints.overlay` adds `zephyr,disabling-power-states = <&deeppowerdown>` to
  `&porta`…`&portd`. That state, and only that one, is what the SoC dtsi already names in
  `core_domain`'s `onoff-power-states`, so no new reference-manual claim is made here.
  Only the first four instances are listed because `porte` does not exist on MCXA153 and
  `portf` only on some parts, and a missing label is a build error.
- the driver never calls `pm_policy_device_power_lock_get()`, and should not: a pad
  configuration is not an operation in flight. The phase checks that nothing holds a lock
  on the pin-mux's behalf.

### System-managed sweep — `overlay-pm-sysmanaged.conf`
```sh
... -- -DEXTRA_CONF_FILE=overlay-pm-sysmanaged.conf
```
One forced **Deep Sleep** transition with `pm_suspend_devices()`/`pm_resume_devices()`
around it. Deep Sleep only gates clocks, so every instance must come back `ACTIVE` with
its gate open, and the console must survive without help.

### Deep Power Down — `overlay-pm-sysmanaged.conf` + `dpd-{mcxn,mcxa}.overlay`
```sh
... -- -DEXTRA_CONF_FILE=overlay-pm-sysmanaged.conf \
       -DEXTRA_DTC_OVERLAY_FILE=dpd-mcxn.overlay
```
The only layer that reaches the driver's `TURN_ON` for real. Enabling `&deeppowerdown`
arms the SoC's `core_domain`, which lists that state in `onoff-power-states`; the sweep
suspends the domain, the domain hands every child a `TURN_OFF`/`TURN_ON` pair.

After the transition the phase samples every gate and re-opens any that came back closed
**before** printing anything or touching the console, then re-initialises the
LP_FLEXCOMM parent and the LPUART — and *not* the pin-mux instances. That omission is
deliberate and is the second half of the evidence: `device_init()` on the LPUART
re-applies its pin state, which writes a PCR, which faults unless something has already
re-opened that instance's gate. Surviving console output from that point on is the proof
that the domain's `TURN_ON` ran first. (The LPADC case still re-initialises the PORT
devices by hand; that workaround is what this PR removes the need for.)

Expected: every observable gate open, every instance `ACTIVE` — except PORT5 on
`frdm_mcxa577`, as described above.

## Helper

```sh
scripts/run_port.sh <baseline|device|runtime|system|sysmanaged|dpd> [-b BOARD] [--flash]
```
(the `dpd` mode picks the family overlay from the board target).

## Notes / follow-ups

- **A build with device PM but without `CONFIG_PM` faults at boot on every board this
  case targets, and it is not the pin-mux's fault.** `pd_pm_action()` in
  `drivers/power_domain/power_domain_soc_state_change.c` opens with
  `pm_state_next_get(_current_cpu->id)->state`, and with `CONFIG_PM=n` that function is a
  stub returning `NULL`. `CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE` reaches it during
  `PRE_KERNEL_1`, because `kernel/device.c` calls `pm_device_runtime_auto_enable()` right
  after each device's init and enabling runtime PM on an ACTIVE device runs `SUSPEND` on
  it. The fault therefore lands before the console exists and the board prints nothing at
  all — the runtime layer captures zero bytes rather than a stack dump. Returning 0 when
  `pm_state_next_get()` reports `NULL` fixes it; verified on `frdm_mcxn947`. Any board
  whose devicetree grows a `power-domain-soc-state-change` node inherits this, so it wants
  fixing in the same series that adds the node.
- The pad configuration itself is not asserted on, only the gate. Proving that the PCR
  contents survive a gate cycle would mean reading the PCR array, and the sample
  deliberately never touches it; the Deep Power Down layer gets a stronger answer for
  free, because the console only works if the pin state was successfully re-applied.
- `clock_control_off()` is not exercised: it has no PORT case, so calling it would assert
  nothing about the driver.
- The two low-power layers cannot run on MCXA: the board prints every check and then goes
  quiet at the transition, which is the known MCXA wake-up defect and says nothing about
  the pin-mux. `dapeng remote control powercycle` brings the board back.
- Not covered: the `cpu1` clusters and the `_ns`/TrustZone board variants.

