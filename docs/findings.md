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
| `vref-output-not-reenabled-after-dpd` | vref | A consumer's held output and selected mode were not restored after register loss | `drivers/regulator/regulator_nxp_vref.c` | corrected in `enable-vref-device-pm`; hardware DPD verification pending |
| `ctimer-pm-actions-were-empty` | ctimer | the driver registered a `pm_device` whose `SUSPEND`, `RESUME` and `TURN_OFF` actions were all an empty `break;`, so every transition was reported to the core and none of them reached the hardware; the clock ran and an armed alarm stayed armed through a suspend | `drivers/counter/counter_mcux_ctimer.c` | fixed, verified on hardware |
| `ctimer-clock-gate-missing-from-syscon` | ctimer | `clock_control_mcux_syscon.c` carried no `MCUX_CTIMERn_CLK` case in either `on()` or `off()`, so a driver asking for its CTIMER clock to be gated was answered 0 and nothing happened — the same shape as `port5-clock-gate-never-reopened`, but for every instance on every family | `drivers/clock_control/clock_control_mcux_syscon.c` | fixed, verified on hardware |
| `ctimer-power-lock-held-while-suspended` | ctimer | the `pm_policy` device power lock was taken from `start()` and released only from `stop()`, so a counter the consumer left running kept blocking the states it declares for as long as the device was suspended — the system is held out of a state for a block that is not counting and has nothing to lose there | `drivers/counter/counter_mcux_ctimer.c` | fixed, verified on hardware |
| `ctimer-power-lock-leaks-after-turn-off` | ctimer | whether the lock was held was inferred from `TCR[CEN]` rather than recorded, and a turn-off resets the register block: the bit reads back clear while the lock is still taken, so the next `stop()` released nothing and the states stayed blocked for the rest of the boot | `drivers/counter/counter_mcux_ctimer.c` | fixed, verified on hardware |
| `sctimer-suspend-left-the-clock-running` | sctimer | `SUSPEND` halted the counter and parked the configured outputs but never gated the SCT AHB clock, so a suspended SCTimer kept drawing it and the PM state was reported with no power effect | `drivers/pwm/pwm_mcux_sctimer.c` | fixed, not yet verified on hardware |
| `sctimer-clock-gate-missing-from-syscon` | sctimer | `clock_control_mcux_syscon.c` carried no `MCUX_SCTIMER_CLK` case in either `on()` or `off()`, so a driver asking for its SCTimer clock to be gated was answered 0 and nothing happened — the same shape as `port5-clock-gate-never-reopened` and `ctimer-clock-gate-missing-from-syscon` | `drivers/clock_control/clock_control_mcux_syscon.c` | fixed, not yet verified on hardware |
| `sctimer-turn-on-left-the-block-counting` | sctimer | `TURN_ON` re-ran init and replayed the channel, which starts the counter, and did nothing to stop it again; `pm_device_driver_init()` then records the device `SUSPENDED` under runtime PM, so the block booted counting and driving a pin behind a PM state that said it was not | `drivers/pwm/pwm_mcux_sctimer.c` | fixed, not yet verified on hardware |
| `sctimer-turn-on-dropped-the-init-error` | sctimer | `TURN_ON` discarded the return value of the bring-up it calls, so a failed init was reported to the PM core as a successful turn-on | `drivers/pwm/pwm_mcux_sctimer.c` | fixed, not yet verified on hardware |
| `sctimer-rejected-call-still-recorded` | sctimer | `set_cycles()` wrote the channel into `pwm_channel_config[]` before applying it, and three paths below that return `-ENOTSUP`, so a call the consumer was told had failed was still what `TURN_ON` replayed after a power cycle | `drivers/pwm/pwm_mcux_sctimer.c` | fixed, not yet verified on hardware |
| `sctimer-capture-off-stopped-the-pwm` | sctimer | `disable_capture()` ended in an unconditional `SCTIMER_StopTimer()`, and the counter is shared, so turning a capture channel off stopped every PWM output driving from it | `drivers/pwm/pwm_mcux_sctimer.c` | fixed, not yet verified on hardware |
| `sctimer-no-policy-constraint` | sctimer | the node declared no `zephyr,disabling-power-states` and the driver held no policy lock, so system PM was free to enter Deep Sleep — where MCX Nx4x RM Rev. 6_RC2 Table 336 records the SCT as `Static` — and silently stop an output the consumer was driving | `drivers/pwm/pwm_mcux_sctimer.c`, `dts/arm/nxp/mcx/mcxn/nxp_mcxnx4x_common.dtsi` | fixed, not yet verified on hardware |
| `mrt-pm-actions-were-empty` | mrt | the module device's `SUSPEND`, `RESUME` and `TURN_OFF` actions were an empty `break;` and `TURN_ON` re-ran init and left the bus clock on, so every transition was reported to the core and none of them reached the hardware | `drivers/counter/counter_nxp_mrt.c` | fixed in `enable-mrt-device-pm`, verified on hardware |
| `mrt-clock-gate-missing-from-syscon` | mrt | `clock_control_mcux_syscon.c` handled `MCUX_MRT_CLK` and `MCUX_FREEMRT_CLK` in `on()` only, so `clock_control_off()` on an MRT clock was answered 0 and nothing happened — the same shape as `ctimer-clock-gate-missing-from-syscon` | `drivers/clock_control/clock_control_mcux_syscon.c` | fixed in `enable-mrt-device-pm`, verified on hardware |
| `mrt-channels-had-no-pm-device` | mrt | the counter devices are the `nxp,mrt-channel` children, and they were defined with no `pm_device`, so `pm_device_runtime_get()` on the device a consumer holds returned 0 and did nothing | `drivers/counter/counter_nxp_mrt.c` | fixed in `enable-mrt-device-pm`, verified on hardware |
| `mrt-power-lock-unbalanced` | mrt | `stop()` released the `pm_policy` device power lock unconditionally and `start()` took it again on a running channel, with nothing recording whether the channel held it; a stop on an unstarted channel trips the unbalanced-put assertion, or drives the state's count negative and blocks it for the rest of the boot | `drivers/counter/counter_nxp_mrt.c` | fixed in `enable-mrt-device-pm`; the lock following each channel is verified on hardware, the unmatched put itself is not exercised |
| `mrt-modcfg-read-before-clock` | mrt | `init_common()` read `MODCFG[NOC]` for the channel count before deasserting the reset and before enabling the bus clock | `drivers/counter/counter_nxp_mrt.c` | fixed in `enable-mrt-device-pm`; bring-up verified on hardware |
| `mrt-top-value-reads-zero-while-idle` | mrt | `counter_get_top_value()` read `INTVAL[IVALUE]`, which is 0 on an idle channel, so it contradicted the top value just accepted by `counter_set_top_value()` | `drivers/counter/counter_nxp_mrt.c` | fixed in `enable-mrt-device-pm`, not exercised on hardware |
| `mrt-isr-clears-inuse` | mrt | the ISR cleared `STAT[INTFLAG]` with a read-modify-write, and `STAT[INUSE]` is also write-1-to-clear, so it cleared an `INUSE` that read as 1 | `drivers/counter/counter_nxp_mrt.c` | fixed in `enable-mrt-device-pm`, not observable: the driver never uses `INUSE` |
| `mrt-bring-up-errors-dropped` | mrt | `clock_control_on()` in `init_common()`, `init_common()` in the turn-on action and `clock_control_get_rate()` in `get_freq()` all had their errors discarded | `drivers/counter/counter_nxp_mrt.c` | fixed in `enable-mrt-device-pm`, not exercised on hardware |
| `mrt-channel-suspended-while-counting` | mrt | a channel's `SUSPEND` releases its policy lock and touches no register, because the clock belongs to the module; if a sibling still holds the module, a channel released while running keeps counting and delivering callbacks while the core records it `SUSPENDED` and nothing protects it. The `device` layer shows a suspended channel at `STAT[RUN]=1` | `drivers/counter/counter_nxp_mrt.c` | open, fix direction not decided |
| `pd-soc-state-change-null-deref` | port, vref, ctimer | `pd_pm_action()` dereferences a NULL `pm_state_next_get()` result when `CONFIG_PM=n`, faulting during device init and blocking the runtime layer | `drivers/power_domain/power_domain_soc_state_change.c` | open at Zephyr `1ee3b93134be` |
| `pd-soc-state-change-no-turn-on-under-runtime-pm` | port, vref | a child of such a domain never receives `TURN_ON` under runtime device PM: the domain is runtime-suspended before the child initialises, and it forwards `TURN_ON` only on the system state changes it lists | `drivers/power_domain/power_domain_soc_state_change.c` | open |
| `vref-unconfigured-under-runtime-pm` | vref | consequence of the row above: the vref block never receives its devicetree configuration, because the `TURN_ON` that carries it never runs. `CSR` reads `0x00010800` — the mode bits from `regulator_common_init()` are there, `ICOMPEN`/`CHOPEN`/`REGEN` are not | `drivers/regulator/regulator_nxp_vref.c` or the domain driver | open |
| `mcxe247-idle-stack-too-small-for-pm-sweep` | ftm, frdm_mcxe247 | the 320-byte idle stack overflows during the system-managed PM sweep with immediate logging; the FTM case requires 1024 bytes in `tests/ftm/boards/frdm_mcxe247.conf` | board or SoC defconfig, not a peripheral driver | open; case-local workaround |
| `mcxa-suspend-to-idle-never-wakes` | all, MCXA only | on MCXA a `k_msleep()` that lands in `PM_STATE_SUSPEND_TO_IDLE` never returns; reproduced with a 20-line application that touches no peripheral under test. It is why the `sysmanaged` and `dpd` layers are recorded `blocked`, not `fail`, on every MCXA board | SoC PM glue, not a peripheral driver | open |
| `mcxa153-runtime-default-enable-hangs-at-boot` | ctimer, a153 only | the `runtime` and `system` layers hang before suite setup with `CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE`; the `device` layer passes | unattributed | open |

## What `fixed` means, and which tree it is fixed in

`fixed` is a statement about a tested revision, not the current checkout. The five
`lpadc-*` rows are verified by
[lpadc.json](../results/lpadc.json).
Its `zephyr` field identifies the revision; all six layers pass on `frdm_mcxn947/mcxn947/cpu0`.

So a failing layer is not by itself evidence of a new defect. Read the record's `zephyr` field
first: a verdict is only about the commit recorded beside it.

## Two of these are not driver defects

`pd-soc-state-change-null-deref` and `pd-soc-state-change-no-turn-on-under-runtime-pm` live in the
shared power-domain plumbing, and reach every board whose devicetree has a
`power-domain-soc-state-change` node.

`mcxa-suspend-to-idle-never-wakes` is an environment defect. It blocks two layers on a whole family
and says nothing about the peripheral, which is why the record distinguishes `blocked` from `fail`.

## VREF replay and runtime contract

The [VREF case](../tests/vref/README.md) requires the driver to restore a held output and the
consumer's last accepted mode, without changing the common regulator reference count.
An unused output must remain off. The driver holds a runtime reference between its enable
and disable operations; configuration accesses use temporary references.

The shared-domain findings above describe the historical failing revisions. At Zephyr
`a915b2f12675`, `power_domain_soc_state_change.c` already checks for a NULL next state and
forwards `TURN_ON` on runtime resume. Those fixes are prerequisites of the current VREF
runtime case, not new changes made by the VREF series. Each result applies only to its tested source.
