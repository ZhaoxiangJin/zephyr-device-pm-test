# Adding a driver case

A case is one peripheral driver, exercised through the six layers of
[pm-layers.md](pm-layers.md). Copy the nearest existing one and work through this list.

```
tests/<ip>/
  CMakeLists.txt        one line per layer file, plus EXTRA_ZEPHYR_MODULES
  Kconfig               case-specific options only (a loopback, an alternate wiring)
  prj.conf              CONFIG_ZTEST=y, CONFIG_PM_TEST=y, CONFIG_<DRIVER>=y
  case.yaml             the DT compatibles and driver sources this case covers
  testcase.yaml         one scenario per layer, platform_allow
  constraints.overlay   which SoC states stop this peripheral working
  boards/<target>.overlay   per-board wiring; omit the directory if there is none
  src/main.c            the ZTEST_SUITE and its hooks, nothing else
  src/<ip>.c <ip>.h     the wiring and the one hardware truth this case asserts on
  src/test_control.c    the control: this case really does drive the peripheral
  src/test_device.c test_runtime.c test_system.c test_lowpower.c
  README.md             what is observable and why; <=80 lines
```

## 1. Find the hardware truth first

This is the step that decides whether the case is worth writing, so do it before any code.

A case must be able to distinguish "device suspended" from "device active" at runtime. If the
driver's API can fail on a suspended device — as `adc_read()` can — that is the observation. **Most
drivers cannot**, and then you must assert on the peripheral register the PM callback touches:
`lpcmp` reads `CCR0.CMP_EN`, `port` reads the clock gate out of the clock controller, `vref` reads
`CSR`.

Put that accessor in `src/<ip>.c` and nothing else. A case whose `<ip>.c` observes nothing cannot
report anything, however many layers it builds.

Read the register out of the *right* block. `port` reads its gate from the clock controller rather
than from a PCR, because a PCR only answers while the block is clocked — touching it is exactly what
must not be done to decide whether the clock is on.

## 2. One file per layer

Each `src/test_<layer>.c` holds that layer's `ZTEST`s and is compiled only when its layer symbol is
set:

```cmake
target_sources_ifdef(CONFIG_PM_TEST_LAYER_DEVICE app PRIVATE src/test_device.c)
```

`src/test_control.c` is the exception: it is compiled into every layer, because a layer verdict from a
build whose peripheral never worked at all means nothing.

`test_lowpower.c` serves both `sysmanaged` and `dpd`: the sequence is the same and only the state
entered differs, which `pm_test_enter_transition()` picks from the layer symbol. Reviving the console
afterwards is a separate call, `pm_test_console_resume()`, because the case decides whether the
pin-mux may be re-initialised on the way back — for `port` it may not, that being the subject.

Order-dependent steps — sample, force the transition, sample again — live inside **one** `ZTEST`
function. ztest does not guarantee ordering between functions and this harness never assumes it. A
test that must observe the state the device *booted* in therefore has to be the head of such a
sequence, not a test of its own.

Anything a test needs to be true before it runs goes in the suite's `setup`, which ztest does
guarantee runs first. `setup` cannot fail the suite, so record its result and assert it from
`before`, which can.

## 3. Skip honestly

A peripheral instance that cannot be observed on a given SoC must say so, not pass quietly.
`port` on MCXN is the worked example: `portf` has no gate constant in that HAL, so the case reports
it as not observable and asserts nothing about it. Use `TC_PRINT` per instance and
`ztest_test_skip()` only when *no* instance is observable.

## 4. Wire the boards

One `boards/<board_target>.overlay` per board you support; Zephyr matches them by board target name.
Adjust `prj.conf` for the driver's own Kconfig. If the SoC dtsi already enables the nodes for every
board, skip the directory entirely.

## 5. Declare the case

`case.yaml` names the DT compatibles and driver sources:

```yaml
case: lpadc
ip: LPADC
compatibles: [nxp,lpc-lpadc]
drivers: [drivers/adc/adc_mcux_lpadc.c]
```

Without it the Device PM report cannot tell which IP the case exercises, so the case is invisible
there however green it is locally.

`testcase.yaml` gets one scenario per layer, named `drivers.pm.<case>.<layer>`, with the layer
selected by `required_snippets` and never by hand-written `EXTRA_CONF_FILE`:

```yaml
  drivers.pm.lpadc.dpd:
    required_snippets: [pm-dpd]
```

The scenario name's last dot-segment **is** the layer name in a run record, and it must be one of the
six in [pm-layers.md](pm-layers.md). Nothing else may appear there — not a board, not an SoC family.
A per-family difference belongs in the snippet, applied from the board target: that is how `pm-dpd`
carries one MCXN-only overlay and every case still has a single `dpd` scenario.

## 6. Write the README, and only the README

`tests/<ip>/README.md` carries what is observable and why, and what each layer is expected to report
today. Keep it under 80 lines.

A defect the case found goes in [findings.md](findings.md) as one row with a stable id. The README
may argue the reasoning at length; it must not be a second copy of the register.

## 7. Run it, then record it

Build-sweep every declared target × every layer, judged by artifacts and not by stdout — concurrent
writers interleave and produce fictitious failures. Check the scenario count too: twister needs
`ZEPHYR_EXTRA_MODULES` to resolve `required_snippets`, and without it every layer but `baseline` is
silently filtered out (see [pm-layers.md](pm-layers.md#running-one-layer)).

Then run on silicon and record it with `scripts/collect_results.py` (see [results.md](results.md)).

A case that exists but has never run is reported as exactly that. That is the honest state.
