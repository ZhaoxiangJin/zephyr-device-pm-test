# The six layers

This file is normative. A layer is defined here, implemented by a snippet under
[../snippets/](../snippets/), named by one `CONFIG_PM_TEST_LAYER_*` symbol, and referenced by every
case's `testcase.yaml` through `required_snippets`. Nothing else may define a layer.

Every case is built once per layer from one source tree. Each layer isolates one mechanism, so a
failure names the broken mechanism instead of "PM is broken".

| Layer | Snippet | Symbol | What it proves |
| --- | --- | --- | --- |
| `baseline` | *(none)* | `PM_TEST_LAYER_BASELINE` | the case really drives the peripheral — the control |
| `device` | `pm-device` | `PM_TEST_LAYER_DEVICE` | the driver's `SUSPEND`/`RESUME` callback, driven directly through `pm_device_action_run()` |
| `runtime` | `pm-runtime` | `PM_TEST_LAYER_RUNTIME` | `pm_device_runtime_get()`/`put()` reference counting, on a device that boots SUSPENDED |
| `system` | `pm-system` | `PM_TEST_LAYER_SYSTEM` | the per-operation `pm_policy_device_power_lock` and the node's `zephyr,disabling-power-states` |
| `sysmanaged` | `pm-sysmanaged` | `PM_TEST_LAYER_SYSMANAGED` | `pm_suspend_devices()`/`pm_resume_devices()` around one forced Deep Sleep |
| `dpd` | `pm-dpd` | `PM_TEST_LAYER_DPD` | the power domain's `TURN_OFF`/`TURN_ON` restoring a register block that came back from reset |

A `dpd` failure is a different claim from a `sysmanaged` failure: the sweep's `SUSPEND`/`RESUME` is
not enough once the peripheral has actually lost power, so `dpd` is the only layer that can catch a
driver with no restore path.

## Running one layer

```sh
BOARD=frdm_mcxn947/mcxn947/cpu0

west build -b $BOARD zephyr-device-pm-test/tests/lpadc -p always            # baseline
west build -b $BOARD zephyr-device-pm-test/tests/lpadc -p always -S pm-dpd  # any other layer
```

`-S <snippet>` is the whole interface. No `EXTRA_CONF_FILE`, no per-family overlay to pick by hand:
`pm-dpd` selects the MCXN or MCXA devicetree overlay from the board target itself. The one exception
is `pm-system`, which needs the case's own `constraints.overlay` — genuinely per-case, so the case's
`testcase.yaml` adds it and a manual build wants
`-- -DEXTRA_DTC_OVERLAY_FILE=constraints.overlay` alongside `-S pm-system`.

Through twister, which is how the matrix is actually run:

```sh
export ZEPHYR_EXTRA_MODULES=/c/repos/workspace/zephyrproject/zephyr-device-pm-test

west twister -T zephyr-device-pm-test/tests -O /c/tw -p $BOARD           # every layer
west twister -T zephyr-device-pm-test/tests -O /c/tw -s drivers.pm.lpadc.dpd -p $BOARD
```

The export is needed because twister resolves `required_snippets` before it configures anything, so
it never sees the module a case's `CMakeLists.txt` adds. Without it every layer scenario is filtered
out with `Snippet pm-* not found` and only `baseline` runs — a green summary covering one sixth of
the matrix, so it is worth checking the scenario count.

A short `-O` is not cosmetic on Windows; see [boards.md](boards.md#windows-outdir).

## Where a layer's configuration lives

In `lib/pm_test/Kconfig`, on the layer symbol itself. A snippet's `.conf` is one line naming the
layer; the symbol `select`s the PM configuration that layer means. That is why there is no list of
`CONFIG_PM_*` to keep in step with the table above: the table, the symbol and the configuration are
the same statement in one place.

Two things deliberately stay outside it. The wakeup timer the forced transition needs to return is
`imply`d rather than selected, so it follows the board's devicetree instead of being asserted for
every board. And `CONFIG_PM_S2RAM` has no prompt at all — it follows from the suspend-to-ram state
that `pm-dpd`'s overlay enables, and `layer.h` asserts that it came out set.

## The layer symbol is checked, not trusted

`include/pm_test/layer.h` asserts each layer's premises at build time. This is deliberate: the two
longest warnings this project ever needed to write down were both about a layer that compiles
cleanly and tests nothing. They are now build failures rather than paragraphs —
[zephyr-pm-notes.md](zephyr-pm-notes.md) keeps the reasoning for a reader who hits one.

- `runtime` without `CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE` — `get`/`put` return 0 and exercise
  nothing.
- `sysmanaged` or `dpd` with `CONFIG_PM_DEVICE_RUNTIME` — `pm_suspend_devices()` skips the device.
- `sysmanaged` or `dpd` without `CONFIG_PM` — there is no sweep at all.
- `dpd` without an enabled `deeppowerdown` state and `CONFIG_PM_S2RAM` — the layer silently
  degrades into `sysmanaged`, i.e. a green run for a transition that never happened.
- any layer with `CONFIG_LOG` but not `CONFIG_LOG_MODE_IMMEDIATE` — the run ends with interrupts
  locked so that the core cannot park in WFI and lose the debug port, so a deferred log is never
  flushed and a passing board captures as truncated.

Three of those five are reachable from a case's `prj.conf`, and were confirmed to fail the build:
turning `CONFIG_PM_DEVICE_RUNTIME` on under `sysmanaged`, naming `CONFIG_PM_TEST_LAYER_DPD` without
the snippet that enables the state, and asking for deferred logging. The other two cannot be
reached that way at all — a `select` in `lib/pm_test/Kconfig` outranks an assignment in a `.conf`,
so `CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE=n` and `CONFIG_PM=n` are both overridden back to `y`.
Those two assertions guard against an edit to the layer symbols themselves, not against a case.

## Adding a layer

A seventh layer means: a new `snippets/pm-<name>/`, a new symbol in `lib/pm_test/Kconfig`, its
premises asserted in `layer.h`, a row in the table above, a `src/test_<name>.c` in each case that
can support it, and a scenario in each of those cases' `testcase.yaml`. There is no other place to
edit — and if you find one, that is the bug.
