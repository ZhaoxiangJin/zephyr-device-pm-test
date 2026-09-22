# LPDAC device-PM test

Exercises the power-management hooks of `drivers/dac/dac_mcux_lpdac.c` across the
MCXN and MCXA families, through the six layers of
[../../docs/pm-layers.md](../../docs/pm-layers.md).

## What is observable

`GCR`, and only `GCR` — it is the one register the PM callbacks write:

| Part of `GCR` | Owner | Checked as |
| --- | --- | --- |
| `DACEN` | `SUSPEND` clears it, `RESUME` puts it back | the output buffer is on / off |
| everything else | the devicetree configuration, rebuilt by `TURN_ON` | captured after `dac_channel_setup()`, then compared |

The configured value is captured at run time instead of being recomputed from the
devicetree, so the same assertion holds on every part and still fails when a reset
value comes back: `GCR` after `channel_setup()` is asserted non-zero in the
`baseline` layer, which is what lets the later layers tell "restored" from "reset".

`DATA` is write-only (MCX Nx4x RM Rev. 6_RC2 §42.7.1.4), so the restored code is
not readable and **no claim here is about the output voltage** — that would need a
meter. `dac_write_value()` returning 0 is the only evidence the code was accepted.

## Boards

`boards/<board_target>.overlay` is applied automatically from the board target name
and supplies the `zephyr,user` DAC wiring; every board dts already enables `&dac0`
and applies `pinmux_dac0`.

| Board target | DAC0 output | Note |
| --- | --- | --- |
| `frdm_mcxn947/mcxn947/cpu0` | J1-4 | |
| `frdm_mcxn947/mcxn947/cpu0/qspi` | J1-4 | XIP variant |
| `mcx_n9xx_evk/mcxn947/cpu0` | J1-4 | |
| `mcx_n9xx_evk/mcxn947/cpu0/qspi` | J1-4 | XIP variant |
| `mcx_n5xx_evk/mcxn547/cpu0` | J1-4 | MCXN547, same block |
| `frdm_mcxa156` | J2-9 (P2_2) | |
| `frdm_mcxa577` | J8-23 (P2_2) | |
| `frdm_mcxa266` | J1-4 (P2_2) | console moved to lpuart3 |
| `frdm_mcxa346` | J1-4 (P2_2) | console moved to lpuart3 |
| `frdm_mcxa366` | J1-4 (P2_2) | console moved to lpuart3 |

Nothing has to be connected to the output pin.

On the three A-xx6 boards `DAC0_OUT_P2_2` **is** `LPUART2_TXD_P2_2`, the board's
console, so applying `pinmux_dac0` would take the console away mid-run and the
result would never be printed. The overlays move the console to lpuart3, the same
move `samples/drivers/dac` makes there.

Excluded, with a reason: `frdm_mcxn236`, `frdm_mcxa153` and `frdm_mcxa344` have no
DAC node at all; `dac2` on MCXN947 is an `nxp,hpdac` and a different driver; the
LPC55S3x parts have an `nxp,lpdac` but no SoC power states to test it against.

## What each layer asserts

| Layer | Assertion |
| --- | --- |
| `baseline` | `channel_setup()` + `write_value()` succeed, the output buffer comes on, and `GCR` carries configuration — the control, compiled into every layer |
| `device` | boots ACTIVE; `SUSPEND` clears `DACEN` and leaves the configuration alone; write and setup are then refused with `-EBUSY` without touching `DACEN`; `RESUME` restores both; a second `RESUME` is `-EALREADY`. A second test clobbers `GCR` by hand and requires `TURN_ON` to rebuild it — and to leave the output off, because putting it back is `RESUME`'s job |
| `runtime` | boots with no consumer, so no output and `channel_setup()` is `-EBUSY`; a reference brings it up, a nested get/put keeps it up, the last put takes the output down, and the next get brings it back with the same configuration and no second `channel_setup()` |
| `system` | the policy lock the application takes covers exactly the states the node declares in `zephyr,disabling-power-states`, nothing is locked before or after, and the output is usable while it is held |
| `sysmanaged` | configuration and output survive one forced Deep Sleep |
| `dpd` | the same across Deep Power Down, where `TURN_ON` really does rebuild the block from reset |

The application, not the driver, holds the policy lock and the runtime reference:
the driver takes none of its own and refuses API calls outside ACTIVE, so "is an
output wanted" stays the consumer's fact. `constraints.overlay` restates the
`zephyr,disabling-power-states` list the SoC dtsi already gives `&dac0`, so the
`system` layer does not depend on that half of the work.

In the `runtime` layer the device may boot `OFF` rather than `SUSPENDED` — a child
of a `power-domain-soc-state-change` domain never gets `TURN_ON` there; see
`pd-soc-state-change-no-turn-on-under-runtime-pm` in
[../../docs/findings.md](../../docs/findings.md). It costs this driver nothing,
because the configuration is written by `channel_setup()`, which needs a reference
anyway. Both states are accepted.

On MCXA the `sysmanaged` and `dpd` layers do not return from the transition for
reasons outside this driver (`mcxa-suspend-to-idle-never-wakes`, same document).
