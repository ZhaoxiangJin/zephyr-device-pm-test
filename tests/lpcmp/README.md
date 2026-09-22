# LPCMP device-PM test

Exercises the power-management hooks of `drivers/comparator/comparator_nxp_lpcmp.c`
across the MCXN and MCXA families, through the first four layers of
[../../docs/pm-layers.md](../../docs/pm-layers.md).

## What is observable

`CCR0.CMP_EN`, read straight out of the peripheral. The driver's PM callback touches
that one bit and nothing else — no register save/restore, and `TURN_ON`/`TURN_OFF`
return `-ENOTSUP` — so the bit *is* the driver's whole PM behaviour.

The comparator API cannot stand in for it: `comparator_get_output()` returns the
`CSR.COUT` latch, so against a disabled block it yields a stale level instead of an
error. An API-level test alone cannot tell a working PM hook from a no-op, which is
the situation most drivers are in; see
[../../docs/authoring-a-case.md](../../docs/authoring-a-case.md).

The register address comes from `DT_REG_ADDR()`, not a literal, so one source works on
every board: on MCXN947 cpu0 the node's `reg` is `0x51000` and its `soc/peripheral`
parent translates it to `0x50051000`, while the MCXA parts place LPCMP0 elsewhere.
`CCR0` is at offset `0x8` on all of them.

## Boards

`boards/<board_target>.overlay` is applied automatically from the board target name.
Each one enables `&lpcmp0` (no board dts does), picks the positive mux input, and
names the GPIO the optional loopback layer drives. The values come from
`tests/drivers/comparator/gpio_loopback/boards/`, so the wiring is already validated.

| Board target | Positive input | DAC | Loopback GPIO |
| --- | --- | --- | --- |
| `frdm_mcxn947/mcxn947/cpu0` (+ `/qspi`) | IN0, J2-17 | 127 of VREFH1 | `gpio1` 12, J2-11 |
| `frdm_mcxn236` | IN0, J2-8 | 127 of VREFH1 | `gpio1` 2, J2-10 |
| `mcx_n9xx_evk/mcxn947/cpu0` (+ `/qspi`) | IN0, J2-17 | 127 of VREFH1 | `gpio1` 1, J2-15 |
| `mcx_n5xx_evk/mcxn547/cpu0` | IN0, J2-17 | 127 of VREFH1 | `gpio1` 1, J2-15 |
| `frdm_mcxa153` | IN0, J2-9 | 127 of VREFH1 | `gpio1` 5, J2-3 |
| `frdm_mcxa156` | IN0, J2-9 | 127 of VREFH1 | `gpio1` 4, J2-1 |
| `frdm_mcxa266` / `344` / `346` / `366` | IN1, J2-17 | 127 of VREFH1 | `gpio1` 4, J2-7 |
| `frdm_mcxa577` | IN2, P1_4 (R132) | 160 of VREFH0 | `gpio0` 19, J6-1 |

The positive input gets no bias-pull — `lpcmp-pinctrl-never-applied` in
[../../docs/findings.md](../../docs/findings.md) — so its level is indeterminate
unless something drives it. That is why no layer asserts on the comparator's output
*value* unless the loopback option is on.

## What each layer asserts

| Layer | Assertion |
| --- | --- |
| `baseline` | the comparator answers at all: a level, `set_trigger()`, `trigger_is_pending()` — the control, compiled into every layer |
| `device` | boots ACTIVE with `CMP_EN=1`; `SUSPEND` clears it; `CCR2` survives the round trip unaided; a second `RESUME` is `-EALREADY`; and `set_trigger_callback()` on a suspended device must not re-enable the block |
| `runtime` | boots SUSPENDED, and `CMP_EN` clear to match; nested `get`/`get`/`put`/`put` suspends only on the last `put` |
| `system` | the states `constraints.overlay` declares are exactly the states the device's power lock covers, and none is left locked after it is released |

`TURN_ON`/`TURN_OFF` are deliberately unexercised: the comparator is in no power
domain here, so the only caller would be the test itself, and `pm_device` forces the
state to `OFF` even when the driver refuses the action — which would report a harness
artefact as a driver defect.

Two layers are **expected to fail** against the current driver
(`lpcmp-boots-enabled-while-suspended` and `lpcmp-set-trigger-callback-reenables`): a
driver fix shows up as `device` and `runtime` turning green.

## Optional GPIO loopback

The only layer that proves a `SUSPEND` stopped the analog comparison rather than
flipping a bit. It needs a jumper between the two pins named in the table above — the
exact pair for any board is in the header of `boards/<board_target>.overlay`:

```sh
west build -b $BOARD . -p always -S pm-device -- -DEXTRA_CONF_FILE=overlay-loopback.conf
```

It drives the positive input high then low and requires the output to track both
edges while ACTIVE and to track neither while SUSPENDED. A missing jumper fails the
suite immediately with a wiring message, so it cannot be mistaken for a defect.

## Follow-up

Unlike the LPADC driver, this one takes no `pm_policy_device_power_lock` of its own,
because a comparator is useful for as long as the application wants its output rather
than for the length of one call. The `system` layer therefore holds the constraint the
way an application has to, and checks the constraint is wired up — not that the policy
*honours* it, which needs the idle-thread and residency setup this case does not have.

`enable-stop-mode` plus a FRO_16K/XTAL32K function clock would keep the comparator
alive in stop modes and change what belongs in `constraints.overlay`. Not exercised.
Not covered either: the `cpu1` clusters and the `_ns`/TrustZone board variants.
