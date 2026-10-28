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

GCR is read only after successful channel setup. Before that, the block can still
be held in peripheral reset and a read can fault. The runtime boot check captures
the PM state and the rejected setup result in suite setup, before the control test
configures the DAC; it does not read the unconfigured register block.

The main and ztest stacks are 2048 bytes for suite setup and immediate logging.

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
| `frdm_mcxa266` | P2_2, not routed by this test | register-only; console stays on lpuart2 |
| `frdm_mcxa346` | P2_2, not routed by this test | register-only; console stays on lpuart2 |
| `frdm_mcxa366` | P2_2, not routed by this test | register-only; console stays on lpuart2 |

Nothing has to be connected to the output pin.

On the three A-xx6 boards `DAC0_OUT_P2_2` **is** `LPUART2_TXD_P2_2`, the board's
console. Their overlays omit the DAC pinctrl states and keep the default console
and its pin routing. The DAC stays enabled and all GCR and PM assertions still
run; these targets make no claim about the DAC pad or pinctrl restoration.

Excluded, with a reason: `frdm_mcxn236`, `frdm_mcxa153` and `frdm_mcxa344` have no
DAC node at all; `dac2` on MCXN947 is an `nxp,hpdac` and a different driver; the
LPC55S3x parts have an `nxp,lpdac` but no SoC power states to test it against.

## What each layer asserts

| Layer | Assertion |
| --- | --- |
| `baseline` | `channel_setup()` + `write_value()` succeed, the output buffer comes on, and `GCR` carries configuration — the control, compiled into every layer |
| `device` | boots ACTIVE; `SUSPEND` clears `DACEN` and leaves the configuration alone; write and setup are then refused with `-EBUSY` without touching `DACEN`; `RESUME` restores both; a second `RESUME` is `-EALREADY`. A second test clobbers `GCR` by hand and requires `TURN_ON` to rebuild it — and to leave the output off, because putting it back is `RESUME`'s job |
| `runtime` | captures SUSPENDED/OFF and a rejected `channel_setup()` before any test configures the DAC; a reference brings it up, a nested get/put keeps it up, the last put takes the output down, and the next get brings it back with the same configuration and no second `channel_setup()` |
| `system` | the driver locks exactly the states the node declares in `zephyr,disabling-power-states`, and only while its output is on: nothing is locked before the write or after the last consumer reference is dropped, and a resume that restores the output restores the lock with it |
| `sysmanaged` | configuration and output survive one forced Deep Sleep |
| `dpd` | the same across Deep Power Down, where `TURN_ON` really does rebuild the block from reset |

The runtime reference is the application's: the driver takes none of its own and
refuses API calls outside ACTIVE, so "is an output wanted" stays the consumer's
fact. The policy lock is the driver's, and it is pinned to that same fact — taken
when the output is turned on, dropped in `SUSPEND`, which under runtime PM is where
the last reference goes away. `constraints.overlay` restates the
`zephyr,disabling-power-states` list the SoC dtsi already gives `&dac0`, so the
`system` layer does not depend on that half of the work.

In the `runtime` layer the device may boot `OFF` or `SUSPENDED`, depending on its
power domain at initialization. Both states are accepted. Configuration belongs
to `channel_setup()`, which requires the consumer to hold a reference.

On MCXA the `sysmanaged` and `dpd` layers do not return from the transition for
reasons outside this driver (`mcxa-suspend-to-idle-never-wakes`, same document).

## Results

Results: [lpdac.json](../../results/lpdac.json).

| Board target | baseline | device | runtime | system | sysmanaged | dpd |
| --- | --- | --- | --- | --- | --- | --- |
| `frdm_mcxn947/mcxn947/cpu0` | pass | pass | pass | pass | pass | pass |
| `mcx_n9xx_evk/mcxn947/cpu0` | pass | pass | pass | pass | pass | pass |
| `mcx_n5xx_evk/mcxn547/cpu0` | blocked | pass | pass | pass | pass | pass |
| `frdm_mcxn947/mcxn947/cpu0/qspi` | blocked | blocked | blocked | blocked | blocked | blocked |
| `mcx_n9xx_evk/mcxn947/cpu0/qspi` | blocked | blocked | blocked | blocked | blocked | blocked |
| `frdm_mcxa156/mcxa156` | fail | fail | fail | fail | fail | fail |
| `frdm_mcxa266/mcxa266` | blocked | blocked | blocked | blocked | blocked | blocked |
| `frdm_mcxa346/mcxa346` | blocked | blocked | blocked | blocked | blocked | blocked |
| `frdm_mcxa366/mcxa366` | blocked | blocked | blocked | blocked | blocked | blocked |
| `frdm_mcxa577/mcxa577` | blocked | blocked | blocked | blocked | blocked | blocked |

Every configuration builds. Hardware verdicts require the matching LP DAC suite,
RunID and success summary; a farm Pass with a different RunID is not verified coverage.
Blocked results include serial/capture failures, SDK installation failure and missing
matching QSPI execution evidence.

Results apply only to the revisions pinned in the JSON. In that record, MCXA156
fails during suite setup: four layers fault on the initial GCR read, while
`runtime` and `system` stop after the layer banner. Its runtime boot-state check
also follows the control write. These coverage limitations are recorded in
[findings.md](../../docs/findings.md), not attributed to the PM driver.
