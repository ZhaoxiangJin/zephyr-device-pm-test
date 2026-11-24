# SCTimer device PM

Tests `drivers/pwm/pwm_mcux_sctimer.c` and its SYSCON SCTimer clock gate.

## Lifetime and observations

The consumer holds its runtime reference for as long as PWM is needed.
A setter outside ACTIVE returns `-EBUSY`. SUSPEND parks configured outputs
at inactive polarity and gates the clock; RESUME restarts them.
TURN_ON replays saved configuration halted/gated. Zero pulse requests inactivity, not reference release.

- Read the AHB gate from SYSCON using `SCT_CLOCKS`, not from gated SCT registers.
- Accessors require an open gate; diagnostic peek/clobber borrows and returns it.
- Compare live `MATCHREL[0..1]` and `OUT[0].SET/CLR` images, not reset defaults.
- Poll COUNT for movement; fixed-delay sampling can alias whole PWM periods.
- Sample OUTPUT: half duty toggles; zero/full duty stays inactive/active.
- An unused last-match witness distinguishes retention from actual DPD power loss.

## Layer coverage

| Layer | Assertions |
|---|---|
| All | Normal/inverted half, zero and full duty; counter movement; restore normal PWM |
| Device | Park/gate and resume; halted/gated TURN_ON replay; inverted/zero replay; rejected requests do not replace saved output |
| Runtime | OFF/SUSPENDED boot and API refusal; nested usage counts; final-put park/gate; three reacquire cycles |
| System | Generated enabled disabling states exist; lock acquisition, release and reacquisition; runtime put results |
| Sysmanaged | Image and waveform restored; retention witness survives |
| DPD | Image and waveform restored; power-loss witness disappears |

Suite setup saves DUT boot observations before acquiring the reporting console's
separate runtime reference. Teardown releases the console. Boot checks therefore
do not depend on ztest order. Markers bracket unreferenced API checks.
Each runtime test establishes its own state; cleanup reports leaked references
and restores failed device-action tests from OFF/SUSPENDED.

## Board and running

Declared targets: `frdm_mcxn947/mcxn947/cpu0` and its `/qspi` variant.
The enabled `sc_timer` routes OUT0 to PIO2_2. This register-observation case
needs no jumper; the separate in-tree `pwm_pm` loopback uses PIO2_2 -> PIO2_6.
Other MCX N boards do not enable this node; MCX A has no SCTimer.
Only DEFAULT pinctrl exists here, so SLEEP may return tolerated `-ENOENT`.

From the west workspace:

```text
west build -b frdm_mcxn947/mcxn947/cpu0 zephyr-device-pm-test/tests/sctimer -S pm-runtime
```

Other layers use `pm-device`, `pm-system`, `pm-sysmanaged` or `pm-dpd`; baseline has no snippet.
Manual system builds also need `-- -DEXTRA_DTC_OVERLAY_FILE=constraints.overlay`.
See [PM layers](../../docs/pm-layers.md) and [result records](../../docs/results.md).

## Verified result

[sctimer.json](../../results/sctimer.json), 2026-11-23:
[Dapeng job 2373147](https://dapeng.nxp.com/app/request/2373147/testcases/).
Tested Zephyr: `9ec5b081ad265df1c050e9bcfe9a9e23d1e82d01`.
Tested harness: `50af5a4deb75883a849b970b6b45ceef59439201`.
The case code at `e74fe52` matches the tested harness; the result keeps
the actual tested SHA rather than relabelling it.

| Target | baseline | device | runtime | system | sysmanaged | dpd |
|---|---|---|---|---|---|---|
| `frdm_mcxn947/mcxn947/cpu0` | pass | pass | pass | pass | pass | pass |

All six builds and hardware runs passed: 2/5/6/4/3/3 subtests respectively,
23 PASS, 0 FAIL, 0 SKIP, with matching banners/RunIDs and complete UART summaries.
Complementary local checks passed device 5/5 and runtime 6/6. In-tree PWM policy
passed four output checks and skipped two observer-PM checks, not another result
layer. Image hashes and raw logs remain in campaign artifacts.

## Limits

QSPI was declared but not run. Single-channel results do not prove arbitration.
No pad voltage/mux retention, capture measurement/ownership, capture-only startup,
capture disable, error injection, or exact duty fidelity after period changes is
qualified. Forced low-power replay does not prove output counts through a state
declared to stop SCTimer. Fixed-source findings and limits are in
[findings.md](../../docs/findings.md); this result does not certify other revisions.
