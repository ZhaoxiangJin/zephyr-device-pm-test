# WWDT

MCXN/MCXA `nxp,lpc-wwdt` through `drivers/watchdog/wdt_mcux_wwdt.c`.
The hardware assertions observe `wwdt0`; MCXA577 also instantiates `wwdt1` for
multi-instance build coverage. This is not WWDT1 hardware or independent-lock
coverage.

## Lifetime and observable state

The watchdog starts once per image, in `WDT_FLAG_RESET_NONE` mode with a 10 s
timeout. All idle tests run in suite setup before that start.
The running suite holds its runtime reference until the terminal disable suite.
The shared running session uses options=0. Pause-option rejection is outside
this case's scope; neither sleep-pause nor debugger-pause support is claimed.

- Read the bus gate from SYSCON/MRCC, not from an unclocked WWDT.
- Compare `MOD`, `TC`, `WINDOW` and `WARNINT` with status flags masked out.
- Observe `TV` decreasing and a legal feed reloading it.
- Repeated setup or running suspend must neither stop nor feed.
- Terminal disable halts the functional divider, uninstalls the timeout, releases
  constraints and permits the consumer's runtime put. It does not permit restart.
- The system layer checks the generated, enabled disabling states against policy
  locks. A property with no generated states cannot pass.

## Expected per layer

| Layer | Evidence |
| --- | --- |
| `baseline` | supported setup/feed work; no PM assumed |
| `device` | idle suspend/turn-on leave the gate closed, resume opens it; setup while inactive fails; running suspend is `-ENOTSUP`, repeated setup is `-EBUSY`; resume after disable cannot restart counting |
| `runtime` | boot is inactive with the gate closed; get allows setup; a running final put is `-ENOTSUP`; after disable put succeeds and a later get leaves the counter halted |
| `system` | exact running constraints; rejected APIs preserve them; successful disable releases them |
| `sysmanaged` | idle Deep Sleep retains configuration; running ordinary Sleep preserves counting; after disable Deep Sleep leaves the counter halted |
| `dpd` | idle DPD clears a TC sentinel while RAM configuration survives; running policy excludes DPD; after disable DPD clears WDEN without authorizing a new session |

The running low-power test forces only ordinary Sleep, and observes PM entry
notifications. It does not force DS/PD/DPD around policy locks. A passing idle DPD
round trip is not evidence of continuous watchdog supervision during DPD.
The terminal suite runs explicitly after the running suite. It observes a quiet
interval of twice the timeout and tests errors after uninstall. The low-power
layers repeat the quiet interval after their post-disable transition.

The framework suite runner selects the running and terminal stages through
predicates, retaining suite execution statistics and the final run-all check.
Each running test confirms its feed reloaded TV before continuing. Reload
observation is bounded to 2 ms, and countdown samples allow the preceding reload
to settle before comparing TV. A nondecreasing sample reports both TV values,
elapsed time, gate, HALT and MOD. Setup reports pre-start, install, runtime-get
and start boundaries so an incomplete boot can be located without treating it
as a watchdog verdict.

## Scope and execution

`testcase.yaml` lists the MCXN/MCXA targets. The case does not configure SCG or
low-power clock sources and does not relax the SoC's restrictions in an overlay.
There is no `constraints.overlay`: the driver DTS changes are prerequisites.

The counter and its feed-window position are not claimed to survive power loss.
The timeout callback must not fire during this run. Register-only assertions do
not prove oscillator accuracy or real watchdog reset behavior; use separate,
controlled reset tests for early-feed and timeout behavior.

Use a fresh boot for each new session. Disable does not make this driver's
watchdog restartable, and a running watchdog cannot use an asynchronous final put.
Separate reset-enabled tests are required to check actual watchdog reset behavior;
this interrupt-only harness is not a substitute for those checks.

## Historical result

All six layers passed on the earlier revisions and 11 targets recorded in
[results/wwdt.json](../../results/wwdt.json). The record identifies the tested
Zephyr and harness revisions; this result does not extend the coverage limits
above or qualify the source now pinned by this CI branch's west manifest.
