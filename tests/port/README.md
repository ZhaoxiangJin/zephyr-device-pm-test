# PORT pin-mux device-PM test

Exercises the power-management hooks of `drivers/pinctrl/pinctrl_nxp_port.c` across
the MCXN and MCXA families, through the six layers of
[../../docs/pm-layers.md](../../docs/pm-layers.md).

The pin-mux driver has no PM handler in mainline, so every layer past `baseline`
measures behaviour that a driver commit has to supply: `TURN_ON` re-opening the
instance's clock gate, and `SUSPEND`/`RESUME`/`TURN_OFF` doing nothing. Without it
`pm_device_action_run()` answers `-ENOSYS` and the layers fail on that, which is a
statement about the tree and not about the harness — see
[../../docs/findings.md](../../docs/findings.md) on which tree a verdict belongs to.

## What is observable

The SoC's pin-mux clock gate, and only that: nothing in the pinctrl API takes a
device, so "this pin-mux is suspended" cannot be seen through a function call. The bit
is read out of the clock controller — `SYSCON->AHBCLKCTRL0..3` on MCXN,
`MRCC0->MRCC_GLB_CCn` on MCXA, the readable side of the write-only aliases the HAL
writes through.

No file in this case touches a PCR. They only answer while the block is clocked, so
reading one to find out whether the clock is on is precisely the wrong instrument: the
answer arrives as a bus fault.

Nothing in a running build can close a gate either — `clock_control_off()` has no PORT
case at all and just returns 0 — so `src/test_device.c` closes it with the HAL to stand
in for the state the block comes back in after a power loss, then requires `TURN_ON` to
re-open it. Nothing is printed or judged inside that window, because the console's own
pin-mux may be the instance being gated, and whatever the driver did the gate is
re-opened by hand before anything is asserted.

## Boards

No `boards/` directory: the `nxp,port-pinmux` nodes are enabled by the SoC dtsi on
every supported board and need no wiring, so `src/port.c` walks whatever is enabled and
derives each instance number from `DT_CLOCKS_CELL(node, name) - MCUX_PORT0_CLK`. A
`BUILD_ASSERT` per node requires the clock controller to be `nxp,lpc-syscon`, which is
what limits the case to MCXN and MCXA — Kinetis, MCXC, MCXE and MCXL wire the same
driver to a `sim`/`pcc`/root-clock controller with a different cell layout.

| Family | Instances found | Observable gates |
| --- | --- | --- |
| MCXN947 / MCXN547 / MCXN236 | porta…portf (6) | PORT0…PORT4 |
| MCXA153 | porta…portd (4) | PORT0…PORT3 |
| MCXA156 / MCXA266 / MCXA344 / MCXA346 / MCXA366 | porta…porte (5) | PORT0…PORT4 |
| MCXA577 | porta…portf (6) | PORT0…PORT5 |

MCXN parts declare `FSL_FEATURE_SOC_PORT_COUNT` as 6 but the HAL defines
`kCLOCK_Port0..Port4` only, and their `portf` sits at `0x42000`, outside the window the
other five share. There is nothing to assert for it, and the layers say so rather than
passing quietly.

## What each layer asserts

| Layer | Assertion |
| --- | --- |
| `baseline` | every instance is clocked at init — the control, compiled into every layer, because `pinctrl_mcux_init()` opens the gate itself and every driver that applies a pin state from its own init depends on it |
| `device` | `SUSPEND` and `TURN_OFF` leave the gate alone; a gate closed by hand is re-opened by `TURN_ON` |
| `runtime` | boots SUSPENDED **or OFF**, with the gate open anyway — the claim this case exists for |
| `system` | nothing holds a policy lock on the pin-mux's behalf, in any state |
| `sysmanaged` | one forced Deep Sleep, which only gates clocks: every instance comes back ACTIVE with its gate open |
| `dpd` | the same across Deep Power Down, the only state that really closes these gates and therefore the only one that reaches `TURN_ON` |

`runtime` is always OFF on these SoCs, not SUSPENDED, because the pin-mux nodes name
`&core_domain`: the domain is runtime enabled too and suspends right after its own init,
so `pm_device_is_powered()` already answers false when a pin-mux initialises at
`PRE_KERNEL_1`. The `TURN_ON` never arrives later either —
`pd-soc-state-change-no-turn-on-under-runtime-pm`. For a pin-mux the register view is
unaffected, which is the argument for init opening the gate; the vref case next door
shows the same mechanism with a visible cost.

`dpd` is expected to **fail on `frdm_mcxa577`**: PORT5 has a real gate there, and
`clock_control_mcux_syscon.c` has no `MCUX_PORT5_CLK` case in either family branch, so
`clock_control_on()` returns 0 having done nothing — `port5-clock-gate-never-reopened`.

Unlike every other case, `dpd` here revives the console *without* re-initialising the
pin-mux devices. That omission is the second half of the evidence: reviving the LPUART
re-applies its pin state, which writes a PCR, which faults unless the gate is already
open. Surviving console output is the proof.

## Follow-up

The pad configuration itself is never asserted on, only the gate. The `dpd` layer gets
a stronger answer for free: the console only works if the pin state was re-applied.

Not covered: the `cpu1` clusters and the `_ns`/TrustZone board variants.
