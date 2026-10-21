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

**The per-channel register image** — `CTRL`, `INTVAL[IVALUE]`, `STAT[RUN]`. `CTRL`
carries the interrupt enable the module's turn-on action rewrites; `INTVAL` carries
the period; `STAT[RUN]` says whether the channel is counting. Nothing else in the
block holds them.

**A channel whose top callback keeps arriving**, as the control. It is what makes
"the gate is open" mean the peripheral works rather than that a bit is set.

Two channels are enabled by every scenario, from `channels.overlay`, because no
in-tree board enables more than `mrt0_channel0` and a single channel cannot show the
one thing this IP adds: four channels share one gate and one policy count, so
releasing or stopping one must leave a sibling running.

## What this case does not claim

It makes **no claim about a count value or a period surviving a power cycle**.
`INTVAL` is both the period register and the start trigger — writing a nonzero
`IVALUE` to an idle channel starts it immediately — so there is no way to restore a
period without also restarting a run the consumer did not ask for, and the driver
does not try. A channel comes back idle. The `dpd` assertion is therefore
behavioural: a channel started *fresh* after the round trip has to deliver
callbacks, which it cannot unless the module's `TURN_ON` re-ran the bring-up and
re-connected the shared interrupt.

It does not read the period back through `counter_get_top_value()`. That call
answers from the driver's RAM copy by design, so it would agree with itself whatever
the register held. The register image is read straight out of the block.

It asserts nothing about any family other than MCX N. The driver's compatible also
serves MCX W23x, LPC55S6x, i.MX RT5xx/6xx/7xx and RW612; those parts carry the
channel-to-module power domain reference but no `zephyr,disabling-power-states`,
because their reference manuals have not been read for this IP.

## Expected per layer

| Layer | What the MRT case asserts |
|---|---|
| `baseline` | with `PM_DEVICE=n` a channel counts and its top callback keeps arriving |
| `device` | the module's `SUSPEND` closes the gate and keeps every channel's image, its `RESUME` re-opens it, a channel's own `SUSPEND` touches no register, and `TURN_ON` rebuilds a cleared `CTRL` while leaving the gate closed |
| `runtime` | the channels booted suspended or off with the gate closed, the reference count on the module behaves, and releasing one channel leaves a sibling counting |
| `system` | the policy lock matches each channel's declared states, follows that channel across a suspend and a resume, and is not released while another channel still needs it |
| `sysmanaged` | the sweep keeps the block powered, so the programmed period comes back and the callbacks resume on their own |
| `dpd` | the block really lost power — the period did not come back — and a channel started fresh afterwards delivers callbacks |

`TURN_ON` deliberately does not reprogram `INTVAL`, so the `device` layer also
asserts that a channel comes back idle rather than restarted.

`device` and `sysmanaged` look similar and are not: one drives the callbacks
directly, the other goes through the sweep the PM subsystem runs.

## Results

`results/<date>-mrt-<where>.json`, verdict vocabulary from `docs/results.md`.
