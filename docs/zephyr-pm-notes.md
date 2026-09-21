# Notes on Zephyr's PM subsystem

Facts about Zephyr, not about this repository. They are here because each one produced a layer that
compiled cleanly and tested nothing, and because the next person to write a device-PM test will hit
them in the same order.

Each is asserted at build time by `include/pm_test/layer.h`. If an assertion fired and sent you
here, the paragraph below it is the reasoning.

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

Before layers were named by their own Kconfig symbol, this was a live source of bugs: any `#if` that
selected between the manual step and the sweep step had to test `CONFIG_PM` as well, or the manual
step compiled out of the very layer it belonged to and the build failed on an unused helper. With
`CONFIG_PM_TEST_LAYER_*` the question does not arise — which is the argument for naming a layer
rather than deriving it.

## A power domain hands out `TURN_OFF`/`TURN_ON` only when it is itself swept

The same applies to power domains built on `power-domain-soc-state-change`: the domain device gets
its own `SUSPEND`/`RESUME` from the sweep, and that is what makes it hand its children
`TURN_OFF`/`TURN_ON`. Under runtime PM the domain is never swept, so its children never see those
actions.

Two defects follow from this, both recorded in [findings.md](findings.md):
`pd-soc-state-change-null-deref` and `pd-soc-state-change-no-turn-on-under-runtime-pm`. Neither is
an NXP peripheral driver defect; both reach any board whose devicetree has such a node.
