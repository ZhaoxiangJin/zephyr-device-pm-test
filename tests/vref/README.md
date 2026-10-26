# VREF device-PM test

Exercises `drivers/regulator/regulator_nxp_vref.c` through the six
[PM layers](../../docs/pm-layers.md). Each image is a ztest suite. A successful
run requires `SUITE PASS` and `PROJECT EXECUTION SUCCESSFUL`, with no skipped
checks standing in for the layer under test.

## Contract

The regulator common layer counts consumers. The driver holds one runtime PM
reference from the first `regulator_enable()` to the last `regulator_disable()`.
Configuration and query operations use temporary references; they must not start
an unused output. ADC is disabled in this case so no other consumer holds the
reference under test.

`SUSPEND` stops the analog circuitry without dropping the consumer's request.
`TURN_ON` restores configuration and trim, leaving the output off. `RESUME`
restores the last selected mode and an output only if it was requested.
Mode and voltage choices survive both a disable/enable cycle and register loss.
The driver rejects hardware access to a manually suspended device when runtime
PM is disabled.

## Observable evidence

- CSR configuration bits must match the DT options exactly, including options
  that are false.
- Bandgap, bias and buffer enable bits must be clear while suspended. A cached
  `regulator_is_enabled()` value is not evidence that hardware is running.
- A running bandgap must have both HCBGEN and VREFST set.
- Buffer mode is decoded directly from HI_PWR_LV/BUF21EN, independently of the
  driver's saved mode and public getter.
- The selected voltage must read back after restoration.
- The manual layer corrupts configuration and trim **after** suspension,
  verifies that corruption took effect, and repeats the power cycle.
- The system-managed and DPD layers require entry/exit notifications for the
  requested system state, rather than treating a completed sleep as proof.

The MCXN/MCXA register clock is enabled through the clock controller at power-up.
It remains accessible during device suspension; stopping the analog output is
separate from gating the register clock.
Both families also require the VREF analog-supply bit in SPC ACTIVE_CFG1, restored by
`TURN_ON` through the `nxp,spc` phandle.

These observations do not prove analog accuracy, noise, physical output voltage
or current consumption. Measure VREF_OUT against ground, with the loading and
capacitor required by the board/device documentation, to establish those.

## Layer assertions

| Layer | Assertions |
| --- | --- |
| baseline | Configuration, output stability, last-disable shutdown and mode retention on re-enable. |
| device | Held and unused outputs; all three modes; two consecutive power cycles; multi-consumer references; rejected access while suspended. |
| runtime | Zero usage and no output at idle; exactly one driver runtime reference across multiple regulator references; temporary configuration references; independent external PM reference. |
| system | Only an enabled output holds its DT disabling-state constraints; the first of two disables cannot release them; an active but unused register block holds none. |
| sysmanaged | A real Deep Sleep entry/exit restores the chosen voltage and non-default mode for a held output, and leaves an unused output off. |
| dpd | The same checks across Deep Power Down, when the core domain must rebuild lost register state. |

`constraints.overlay` enables the deepest state so the system layer really has
a constraint to inspect. The disabling-state list itself belongs to the SoC:
Deep Sleep, Power Down and Deep Power Down. The optional low-power analog supply
and bandgap retention are not configured, even on parts that can keep VREF
running in those states. This layer does not force any state.

## Targets

| Board target | Variant |
| --- | --- |
| `frdm_mcxn947/mcxn947/cpu0` | MCXN TRIM2V1 |
| `frdm_mcxn947/mcxn947/cpu0/qspi` | MCXN, XIP |
| `frdm_mcxn236` | MCXN TRIM2V1 |
| `mcx_n9xx_evk/mcxn947/cpu0` | MCXN TRIM2V1 |
| `mcx_n9xx_evk/mcxn947/cpu0/qspi` | MCXN, XIP |
| `mcx_n5xx_evk/mcxn547/cpu0` | MCXN TRIM2V1 |
| `frdm_mcxa577` | MCXA IBIAS_EN and discrete coarse voltage selections |

FRDM-MCXA577 requires a **220 nF capacitor between P2_7 and GND**, as specified
by the SDK VREF board example. Confirm that hardware setup before running this
case; the overlay cannot supply the capacitor.
The DPD snippet places the MCXA577 working set in retained SRAM A3
(16 KiB at `0x20004000`). A1/A2 do not survive DPD; using the unrestricted
512 KiB SRAM region would not test driver restoration reliably.

The MCXA5x common SoC description also covers MCXA286/287, MCXA456/457,
MCXA536/537, MCXA556/557 and MCXA566/567. They have VREF but no additional
board target in this case. Older MCXA families have no matching VREF block.
MCXC uses `nxp,vrefv1`, a different driver. LPC55, MCXW and RT118x users of
`nxp,vref` are outside this MCXN/MCXA hardware matrix.

## Running

From the west workspace:

```sh
west build -b frdm_mcxn947/mcxn947/cpu0 zephyr-device-pm-test/tests/vref \
  -d C:/builds/vref-mcxn-baseline -p always
west build -b frdm_mcxa577 zephyr-device-pm-test/tests/vref \
  -d C:/builds/vref-mcxa-runtime -p always -S pm-runtime
```

Select any other layer with its `pm-*` snippet. For the system layer also pass
`-- -DEXTRA_DTC_OVERLAY_FILE=constraints.overlay`.

Twister needs the module for snippet discovery:

```sh
export ZEPHYR_EXTRA_MODULES=/c/repos/workspace/zephyrproject/zephyr-device-pm-test
west twister -T zephyr-device-pm-test/tests/vref -p frdm_mcxa577 \
  -O C:/builds/vref-twister --ninja --short-build-path
```

The complete declared matrix is seven targets times six layers, 42 images.
It requires the corrected VREF driver and MCXA SoC/clock glue. No layer is an
expected failure. Historical results describe their recorded revisions and do
not establish a pass for the current implementation.

## Verification status

Hardware execution, including DPD, is pending. See the driver's
`tests/drivers/regulator/nxp_vref_pm` for hardware-independent replay and
runtime-reference tests. RAM-register tests exercise driver behavior, not
bandgap settling or board power retention.

Local results for this revision:

- The corrected driver passes 12 checks, with no skips, including the MCXA
  supply/voltage variant and the factory-trim-only variant.
- MCXN947 baseline and MCXA577 runtime images build with ELF and HEX outputs.
- MCXA577 DPD configuration and application/driver object compilation pass;
  that image has not been linked or run.
- The manual and policy-layer sources also pass target-header compilation
  checks; this is not a claim that their complete images or hardware runs pass.
