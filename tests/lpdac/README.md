# LPDAC device-PM test

Exercises the power-management hooks of `drivers/dac/dac_mcux_lpdac.c` across the
MCXN and MCXA families, through the six layers of
[../../docs/pm-layers.md](../../docs/pm-layers.md).

The verified coverage below applies to the source pair pinned in
[Results](#results). It does not certify local modifications or a different
Zephyr/test revision.

## What is observable

`GCR`, and only `GCR` — it is the one register the PM callbacks write:

| Part of `GCR` | Owner | Checked as |
| --- | --- | --- |
| `DACEN` and optional `BUF_EN` | `SUSPEND` clears the implemented enables, `RESUME` restores a requested output | all implemented enables are on / off |
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

The default campaign selects one internal-flash, primary-core variant per
physical board and retains all eight distinct boards. QSPI variants remain
available for explicitly requested XIP coverage but are not part of this result.

On the three A-xx6 boards `DAC0_OUT_P2_2` **is** `LPUART2_TXD_P2_2`, the board's
console. Their overlays omit the DAC pinctrl states and keep the default console
and its pin routing. The DAC node stays enabled and all GCR and PM assertions still
run; these targets make no claim about the DAC pad or pinctrl restoration.

The tested implementation marks these three overlays with
`dac-console-shared-pad`. After a system-managed transition, it captures the
restored GCR, configuration and output-enable state before using
`dac_channel_setup()` to release the shared output for UART reporting.
Assertions still check the pre-quiesce snapshot: `hw(after-resume)` prints that
snapshot and `hw(report-quiesced)` prints the live disabled output. This does
not weaken the output-restoration checks or claim simultaneous DAC/UART use.

Excluded, with a reason: `frdm_mcxn236`, `frdm_mcxa153` and `frdm_mcxa344` have no
DAC node at all; `dac2` on MCXN947 is an `nxp,hpdac` and a different driver; the
LPC55S3x parts have an `nxp,lpdac` but no SoC power states to test it against.

## What each layer asserts

| Layer | Assertion |
| --- | --- |
| `baseline` | `channel_setup()` + `write_value()` succeed, the output buffer comes on, and `GCR` carries configuration — the control, compiled into every layer |
| `device` | boots ACTIVE; `SUSPEND` clears all implemented output enables and leaves the configuration alone; write and setup are then refused with `-EBUSY` without changing the output state; `RESUME` restores both; a second `RESUME` is `-EALREADY`. A second test clobbers `GCR` by hand and requires `TURN_ON` to rebuild it — and to leave the output off, because putting it back is `RESUME`'s job |
| `runtime` | captures SUSPENDED/OFF and a rejected `channel_setup()` before any test configures the DAC; a reference brings it up, a nested get/put keeps it up, the last put takes the output down, and the next get brings it back with the same configuration and no second `channel_setup()` |
| `system` | the driver locks exactly the states the node declares in `zephyr,disabling-power-states`, and only while its output is on: nothing is locked before the write or after the last consumer reference is dropped, and a resume that restores the output restores the lock with it |
| `sysmanaged` | configuration and output survive one forced Deep Sleep |
| `dpd` | the same across Deep Power Down, where `TURN_ON` really does rebuild the block from reset |

The device layer also checks that a configured but unwritten DAC stays disabled
across both action pairs and rejects API calls while OFF. The runtime layer
rejects setup and writes after the last consumer reference is released. The
system layer repeats writes to detect constraint-reference leaks and verifies
that reconfiguring an active output releases its constraint.

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

The tested Zephyr source includes the MCXA wakeup-delay, MCXA156 timer-handoff
and LPUART CORE-domain corrections needed for these transitions. Historical
MCXA failures have separate causes in [findings.md](../../docs/findings.md);
they are not a blanket limitation of the current LPDAC matrix.

## Results

Final result: [lpdac.json](../../results/lpdac.json), dated 2026-10-30, from
[Dapeng job 2340053](https://dapeng.nxp.com/app/request/2340053/testcases/).
All **48 build-and-run configurations** pass: eight boards, one variant per
board, and all six layers. Each board passes 1/4/3/4/2/2 subtests in
baseline/device/runtime/system/sysmanaged/dpd respectively, for **128 passing
subtests**, with no failed or skipped subtests.

| Board target | baseline | device | runtime | system | sysmanaged | dpd |
| --- | --- | --- | --- | --- | --- | --- |
| `frdm_mcxn947/mcxn947/cpu0` | pass | pass | pass | pass | pass | pass |
| `mcx_n9xx_evk/mcxn947/cpu0` | pass | pass | pass | pass | pass | pass |
| `mcx_n5xx_evk/mcxn547/cpu0` | pass | pass | pass | pass | pass | pass |
| `frdm_mcxa156/mcxa156` | pass | pass | pass | pass | pass | pass |
| `frdm_mcxa266/mcxa266` | pass | pass | pass | pass | pass | pass |
| `frdm_mcxa346/mcxa346` | pass | pass | pass | pass | pass | pass |
| `frdm_mcxa366/mcxa366` | pass | pass | pass | pass | pass | pass |
| `frdm_mcxa577/mcxa577` | pass | pass | pass | pass | pass | pass |

Every retained layer has one matching-image boot, matching start/final RunIDs,
a complete ztest summary and final execution success. The result record includes
per-layer task IDs, station IDs, test counts, ELF hashes and UART-log references.
QSPI execution is not claimed by this eight-board campaign.

| Role | Branch | Commit |
| --- | --- | --- |
| Test implementation | [lpdac-farm-safe-ci-20261029](https://github.com/ZhaoxiangJin/zephyr-device-pm-test/tree/lpdac-farm-safe-ci-20261029) | `4f74554b5a97ae45c76986cd3d9cae3ece3ba9e0` |
| Zephyr source fixes | [debug-mcxa156-device-pm-20261030](https://github.com/ZhaoxiangJin/zephyr/tree/debug-mcxa156-device-pm-20261030) | `6f42c26cefee3eb21ce7340e9a67df08f5bc46f6` |
| Dapeng source entry | [debug-lpdac-pm-fixes-20261030-dapeng1](https://github.com/ZhaoxiangJin/zephyr/tree/debug-lpdac-pm-fixes-20261030-dapeng1) | `111a15cede87ac626e1fe1379fa6003add219061` |

The Dapeng entry contains the source fixes plus a CI-only submanifest pinning
the test implementation. The result's `zephyr` field therefore names
`111a15cede87ac626e1fe1379fa6003add219061`, not the source-fix branch tip.
These results apply only to that immutable source pair.
