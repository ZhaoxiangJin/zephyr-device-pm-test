# Findings

Every defect this harness has turned up, in one place. A case's own README carries the
*reasoning* — the register, the reference-manual table, the argument about whose job a fix is.
This table carries the identity and the status, so "what is still open" is one read.

`id` is the join key: `results/*.json` records that reported a finding use the same string, and
so does the commit message that fixes one.

| id | Case | Claim | Owning file | Status |
| --- | --- | --- | --- | --- |
| `lpadc-pm-device-define-not-instanced` | lpadc | `LPADC_PM_DEVICE_DEFINE` / `_GET` are object-like macros, so every instance emits the same `__pm_device_dts_ord_*` symbol; two enabled instances do not build, one instance silently shares a misnamed object | `drivers/adc/adc_mcux_lpadc.c` | fixed, verified on hardware |
| `lpadc-suspended-read-hangs` | lpadc | `adc_read()` against a SUSPENDED converter never returns | `drivers/adc/adc_mcux_lpadc.c` | fixed, verified on hardware |
| `lpadc-read-takes-no-runtime-reference` | lpadc | the read path took no runtime PM reference at all | `drivers/adc/adc_mcux_lpadc.c` | fixed, verified on hardware |
| `lpadc-channel-setup-unbalances-bandgap` | lpadc | `adc_channel_setup()` left the bandgap regulator reference count unbalanced | `drivers/adc/adc_mcux_lpadc.c` | fixed, verified on hardware |
| `lpadc-no-restore-after-dpd` | lpadc | nothing reconfigured or recalibrated the register block after Deep Power Down | `drivers/adc/adc_mcux_lpadc.c` | fixed, verified on hardware |
| `lpcmp-boots-enabled-while-suspended` | lpcmp | `nxp_lpcmp_init()` sets `CCR0.CMP_EN` and only then calls `pm_device_driver_init()`, so with runtime PM the block is left enabled while PM reports SUSPENDED | `drivers/comparator/comparator_nxp_lpcmp.c` | open |
| `lpcmp-set-trigger-callback-reenables` | lpcmp | `set_trigger_callback()` unconditionally sets `CMP_EN`, re-enabling a suspended comparator behind PM's back | `drivers/comparator/comparator_nxp_lpcmp.c` | open |
| `lpcmp-pinctrl-never-applied` | lpcmp | the driver never calls `pinctrl_apply_state()` although its binding includes `pinctrl-device.yaml`, so `pinctrl-0` has no effect (not a PM defect; it is why no layer asserts on the output *value* without the loopback option) | `drivers/comparator/comparator_nxp_lpcmp.c` | open |
| `port5-clock-gate-never-reopened` | port | `clock_control_mcux_syscon.c` handles `MCUX_PORT0_CLK`…`MCUX_PORT4_CLK` and stops, in both family branches, so `clock_control_on()` silently does nothing for a `portf` that has a real gate and `TURN_ON` never re-opens it | `drivers/clock_control/clock_control_mcux_syscon.c` | open |
| `vref-output-not-reenabled-after-dpd` | vref | `TURN_ON` restores neither `regulator-initial-mode` nor the output enable, so after Deep Power Down a consumer still holding an enabled reference reads a dead bandgap and gets no error | `drivers/regulator/regulator_nxp_vref.c` | open |
| `pd-soc-state-change-null-deref` | port, vref | `pd_pm_action()` dereferences `pm_state_next_get()`, which is a stub returning `NULL` when `CONFIG_PM=n`; any device-PM build without system PM faults at `PRE_KERNEL_1` before the console exists | `drivers/power_domain/power_domain_soc_state_change.c` | fixed, verified on hardware |
| `pd-soc-state-change-no-turn-on-under-runtime-pm` | port, vref | a child of such a domain never receives `TURN_ON` under runtime device PM: the domain is runtime-suspended before the child initialises, and it forwards `TURN_ON` only on the system state changes it lists | `drivers/power_domain/power_domain_soc_state_change.c` | open |
| `vref-unconfigured-under-runtime-pm` | vref | consequence of the row above: the vref block never receives its devicetree configuration, because the `TURN_ON` that carries it never runs. `CSR` reads `0x00010800` — the mode bits from `regulator_common_init()` are there, `ICOMPEN`/`CHOPEN`/`REGEN` are not | `drivers/regulator/regulator_nxp_vref.c` or the domain driver | open |
| `mcxa-suspend-to-idle-never-wakes` | all, MCXA only | on MCXA a `k_msleep()` that lands in `PM_STATE_SUSPEND_TO_IDLE` never returns; reproduced with a 20-line application that touches no peripheral under test. It is why the `sysmanaged` and `dpd` layers are recorded `blocked`, not `fail`, on every MCXA board | SoC PM glue, not a peripheral driver | open |

## What `fixed` means, and which tree it is fixed in

`fixed` is a statement about a commit, not about whatever tree happens to be checked out. The five
`lpadc-*` rows are fixed on the branch the record
`results/20260921-lpadc-dapeng-pm-fixes.json` names in its `note`, where all six layers pass on
`frdm_mcxn947/mcxn947/cpu0`. On a checkout without those commits the same six images give a
different answer: `runtime` and `system` fail and `sysmanaged` and `dpd` never print a summary.

So a failing layer is not by itself evidence of a new defect. Read the record's `zephyr` field
first: a verdict is only about the commit recorded beside it.

## Two of these are not driver defects

`pd-soc-state-change-null-deref` and `pd-soc-state-change-no-turn-on-under-runtime-pm` live in the
shared power-domain plumbing, and reach every board whose devicetree has a
`power-domain-soc-state-change` node. They surfaced here because this harness is the first thing to
build those drivers with device PM but without system PM — which is exactly the `device` layer.

`mcxa-suspend-to-idle-never-wakes` is an environment defect. It blocks two layers on a whole family
and says nothing about the peripheral, which is why the record distinguishes `blocked` from `fail`.

## Whose job is `vref-output-not-reenabled-after-dpd`

Genuinely arguable, and [../tests/vref/README.md](../tests/vref/README.md) argues both sides. The
harness does not take a position; it reports that the current behaviour is *silent*, and
demonstrates in the same layer that the block itself recovers on a disable/enable cycle. The mode
half is the weaker one: `regulator_set_mode()` stores nothing in `data`, so a driver could restore
only the devicetree's initial mode, never a consumer's later choice.
