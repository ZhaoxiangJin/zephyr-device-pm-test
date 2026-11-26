# Notes on Zephyr's PM subsystem

Configuration requirements for testing Zephyr device PM.

Layer premises are asserted at build time by `include/pm_test/layer.h`.
The platform and reporting requirements below are additional case prerequisites.

## Runtime PM has to be *enabled*, not just compiled in

`CONFIG_PM_DEVICE_RUNTIME=y` on its own is not enough to test anything.
`pm_device_driver_init()` only leaves a device SUSPENDED and marks runtime PM enabled for it when
`CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE=y` or the node carries `zephyr,pm-device-runtime-auto`.
Without one of those the device is resumed to ACTIVE, runtime PM stays *disabled* for it, and
`pm_device_runtime_get()`/`put()` return 0 without doing anything — a runtime layer that passes
while exercising nothing.

A knock-on effect worth knowing when you write a new case: once runtime PM is genuinely enabled the
device boots SUSPENDED, so any unconditional sanity read in a shared baseline step fails unless
*something* takes a reference. A driver whose API path does that itself — as the LPADC now does —
needs no wrapping; for one that does not, the baseline read has to be wrapped in
`pm_device_runtime_get()`/`put()`, otherwise the control check fails for exactly the reason the
runtime layer is there to document.

## System-managed and runtime device PM are mutually exclusive

`pm_suspend_devices()` skips any device that is busy, is a wakeup source, or has runtime PM enabled.
So a build with both `CONFIG_PM_DEVICE_SYSTEM_MANAGED=y` and
`CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE=y` exercises neither path for the device under test: the
sweep passes it over and nothing else suspends it. That is why `pm-runtime` and `pm-sysmanaged` are
separate snippets and can never be combined.

## `PM_DEVICE_SYSTEM_MANAGED` is set in layers that have no sweep

A second trap in the same symbol: it is `default y if !PM_DEVICE_RUNTIME` inside `if PM_DEVICE`,
with no dependency on `PM`. So the plain `device` layer — `CONFIG_PM_DEVICE=y` and nothing else —
also has it set, even though there is no system PM and therefore no sweep at all.

Use `CONFIG_PM_TEST_LAYER_*` to select manual versus system-managed test steps.
`CONFIG_PM_DEVICE_SYSTEM_MANAGED` alone does not distinguish them.

## Runtime power-domain resume must power up its children

A runtime-suspended `power-domain-soc-state-change` domain can make
`pm_device_driver_init()` leave a child OFF without calling its `TURN_ON`.
Forwarding that action only during system-managed transitions then leaves the
child unconfigured when its first runtime reference arrives.

In the Zephyr revision identified by the [VREF result](../results/vref.json),
the domain's runtime `RESUME` forwards `TURN_ON` to its children. Its `SUSPEND`
also checks for a NULL next system state before deciding whether to forward
`TURN_OFF`. Those shared-domain fixes are prerequisites already present in
the tested source, not changes introduced by the VREF test.
The historical defects and revision-scoped status are in
[findings.md](findings.md).

## Resume delays need a running system timer

When low-power entry stops or resets SysTick, a cycle-based `k_busy_wait()`
cannot complete before the timer is restarted. The legacy PM resume order
restores devices before `sys_clock_idle_exit()`. VREF's enabled-output
`RESUME` needs a bandgap startup delay, so that ordering can prevent the
system-managed or DPD transition from completing.

The [VREF configuration](../tests/vref/prj.conf) enables
`CONFIG_PM_EARLY_SYSTEM_TIMER_RESUME` for system-PM layers. In the tested
source this orders SoC exit post-ops, system timer idle-exit, device resume,
then PM exit notifications. Baseline, device and runtime scenarios override
the option to `n` because they do not enable system PM.

This option is experimental, not a driver-wide requirement to select
unconditionally. Check that SoC post-ops and timer idle-exit do not depend
on still-suspended devices; timer idle-exit can invoke timeout callbacks
before device resume. Manual device/runtime operations with a running timer
do not reproduce this ordering problem.

## Result logging needs space on the reporting thread's stack

With immediate logging and UART runtime PM, printing a result can include
formatting, UART resume and power-domain child callbacks on the caller's
stack. In the MCXA577 VREF system test, the main thread exhausted its
1024-byte stack while printing a successful test result. Hardware reported
`STKOF`, with PSP at its stack limit; the farm saw missing output and timed
out. The test thread's stack was not the overflowing stack.

VREF sets `CONFIG_MAIN_STACK_SIZE=2048`; the
[latest result](../results/vref.json) verifies complete reporting with
immediate logging and all PM assertions retained. Size the actual reporting
thread, and check that the complete DPD working set still fits retained
SRAM. Switching to deferred logging is not equivalent: after
`CONFIG_ZTEST_NO_YIELD` ends the suite with interrupts locked, a deferred
logger cannot finish reporting.
