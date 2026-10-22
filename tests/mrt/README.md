# MRT device PM

One MRT register block, four independent down-counters, and a driver that registers
one counter device per channel plus a module device for init, the shared interrupt
and the clock. So the device a consumer holds is a channel, while the only thing a
suspend can give back belongs to the module — which is what this case is shaped
around.

## What is observable

**The module's bus clock gate**, read out of the clock controller
(`SYSCON->AHBCLKCTRL1[MRT]`) and never out of an MRT register: an MRT register only
answers while the block is clocked, and whether it is clocked is the question. The
MRT has exactly one clock and it both gates the registers and runs the counters, so
gating it is the whole of what a suspend does and it stops all four channels at once.

**The per-channel register image** — `CTRL` (the interrupt enable the module's turn-on
rewrites), `INTVAL[IVALUE]` (the period) and `STAT[RUN]` (whether it is counting).

**A channel whose top callback keeps arriving**, as the control. It is what makes
"the gate is open" mean the peripheral works rather than that a bit is set.

Every scenario enables two channels from `channels.overlay`, because no board enables
more than `mrt0_channel0` and one channel cannot show what this IP adds: four channels
share one gate and one policy count, so releasing or stopping one must leave a sibling
running.

## What this case does not claim

It makes **no claim about a count value or a period surviving a power cycle**.
`INTVAL` is both the period register and the start trigger, so a period cannot be
restored without restarting a run nobody asked for; the driver does not try, and a
channel comes back idle. The `dpd` assertion is behavioural instead: a channel started
fresh has to deliver callbacks, which needs `TURN_ON` to have re-run the bring-up.

It does not read the period back through `counter_get_top_value()`, which answers from
the driver's RAM copy by design and would agree with itself whatever the register held.

It asserts nothing about any family other than MCX N. The driver's compatible also
serves MCX W23x, LPC55S6x, i.MX RT5xx/6xx/7xx and RW612; those parts carry the
channel-to-module power domain reference but no `zephyr,disabling-power-states`,
because their reference manuals have not been read for this IP.

## What each layer asserts

| Layer | What the MRT case asserts |
|---|---|
| `baseline` | with `PM_DEVICE=n` a channel counts and its top callback keeps arriving |
| `device` | the module's `SUSPEND` closes the gate and keeps every channel's image, its `RESUME` re-opens it, a channel's own `SUSPEND` touches no register, and `TURN_ON` rebuilds a cleared `CTRL`, leaves `INTVAL` idle and leaves the gate closed |
| `runtime` | the channels booted suspended or off with the gate closed, the reference count on the module behaves, and releasing one channel leaves a sibling counting |
| `system` | the policy lock matches each channel's declared states, follows that channel across a suspend and a resume, and is not released while another channel still needs it |
| `sysmanaged` | the sweep keeps the block powered, so the programmed period comes back and the callbacks resume on their own |
| `dpd` | the block really lost power — the period did not come back — and a channel started fresh afterwards delivers callbacks |

## Expected per layer

Measured on Dapeng job 2331081 at zephyr `7bc85c05b0f` and harness `81c45f0` —
[../../results/20261022-mrt-dapeng-case-fixes.json](../../results/20261022-mrt-dapeng-case-fixes.json).
`81c45f0` is on `mrt-case`; its `tests/mrt` is identical to `e2ec48e` on `main`. 12 of 12
built, 12 of 12 ran, no assertion failed and none skipped.

| Layer | n947 | n236 |
| --- | --- | --- |
| `baseline` | PASS | PASS |
| `device` | PASS | PASS |
| `runtime` | PASS | PASS |
| `system` | PASS | PASS |
| `sysmanaged` | PASS | PASS |
| `dpd` | PASS | PASS |

In `dpd` a channel enters Deep Power Down with `INTVAL=1500000 RUN=1` and comes back with
`INTVAL=0 RUN=0` and `CTRL[INTEN]` set, then delivers callbacks once started.

The first run, job 2330465
([../../results/20261022-mrt-dapeng.json](../../results/20261022-mrt-dapeng.json)), failed
`device` and `system` on both boards. Both were case defects, fixed in `f5edf94` and
`e2ec48e`; the driver did not change between the two jobs.

One board per SoC devicetree ran. `mcx_n9xx_evk`, `mcx_n5xx_evk` and the `qspi` variants
include the same two dtsi files and were not in either job.
