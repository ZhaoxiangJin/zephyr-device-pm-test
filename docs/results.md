# Run records

A `testcase.yaml` says which layers *exist*. `results/*.json` says which ones have actually **run**,
on which board, and how. The Device PM report in
[zephyr-data](https://github.com/ZhaoxiangJin/zephyr-data) reads both, so a board/IP pair can be
reported as "there is a test" separately from "the test passes on silicon" — a distinction no amount
of static analysis of the Zephyr tree can make.

**These files hold machine facts only.** No analysis, no argument, no defect narrative. A defect
belongs in [findings.md](findings.md); the reasoning behind it belongs in the case's README. The
`note` fields are for run provenance a later reader cannot reconstruct — which station, why a target
was unreachable — and nothing else.

## Producing one

```sh
python scripts/collect_results.py --twister-out /c/tw --where dapeng \
    --job frdm_mcxn947/mcxn947/cpu0=2309422 --station frdm_mcxn947/mcxn947/cpu0=1166@main
```

The verdict per layer comes from twister's own `twister.json`, not from a human reading a console
log. Targets that never reached a board are passed in with `--blocked <target>` and recorded as
such.

### A run on the farm

A station flashes an image; it does not run twister. So build with `--build-only`, flash and capture
per layer as [boards.md](boards.md) describes, and hand the captures back:

```sh
python scripts/collect_results.py --twister-out /c/tf --where dapeng --tag pm-fixes \
    --console baseline=/c/twlog/hw/baseline.txt --console dpd=/c/twlog/hw/dpd.txt \
    --job frdm_mcxn947/mcxn947/cpu0=2314045 --station frdm_mcxn947/mcxn947/cpu0=1166
```

The board's ztest summary becomes the verdict; the target spelling and the Zephyr commit still come
from the build, which is what makes a farm record joinable to a build record. A capture that holds no
ztest output at all is refused rather than recorded as a failure — a console that was never carrying
the board's bytes reads exactly like a board that never booted, and the two must not land in the
register as the same thing. A capture that starts and stops without a summary is a `fail`: the layer
did not pass, and whether it asserted or hung belongs in [findings.md](findings.md).

One file per run, named `<YYYYMMDD>-<case>-<where>.json`. Never edit an old file to record a new
run: add another one. A later run supersedes an earlier one per layer, so history stays readable and
a regression is a diff.

## Schema

```json
{
  "run": "20260912-lpadc-dapeng",
  "date": "2026-09-12",
  "case": "lpadc",
  "where": "dapeng",
  "zephyr": "df86a1df26a",
  "harness": "a6378be",
  "targets": {
    "frdm_mcxn947/mcxn947/cpu0": {
      "job": "2309422",
      "station": "1166@main",
      "note": "provenance only",
      "layers": {"baseline": "pass", "device": "pass", "runtime": "pass",
                 "system": "pass", "sysmanaged": "pass", "dpd": "pass"}
    }
  }
}
```

- `case` matches the directory under `tests/`, which is what ties the run to a DT `compatible`
  through that case's `case.yaml`.
- `targets` keys are board targets in the same slash form as `platform_allow`, so a typo shows up as
  an unmatched target in the report rather than silently counting as untested.
- `layers` keys are the layer names from [pm-layers.md](pm-layers.md) — and nothing else. They are
  the same strings a case's `testcase.yaml` scenarios end in, which is what makes the join work.
- `zephyr` / `harness` are the two commits the run is evidence *about*. Without them a verdict does
  not survive its own branch. A `-dirty` suffix on `harness` means the collector found uncommitted
  changes, so the named commit is not what ran — the record is still useful as a local note, but it
  cannot be cited. Commit first, then regenerate.

## Verdicts

| Verdict | Means |
| --- | --- |
| `pass` | it ran on the board and passed |
| `fail` | it did not pass — it either ran and failed, or did not build. **The one that must never be quietly dropped**: it means a real defect, and that defect gets a row in [findings.md](findings.md) |
| `skipped` | it ran and the case decided the layer is not observable on this SoC, e.g. a peripheral the reference manual says keeps running through the state under test. A statement about silicon, not an absence of one |
| `blocked` | never reached the board — no station, bridge would not come up, or a known environment defect such as `mcxa-suspend-to-idle-never-wakes`. Says nothing about the driver |
| `built` | the image compiles; it was not run. The weakest useful evidence |

Ranking when several runs cover the same layer: `pass`/`fail`/`skipped` all beat `built`, which beats
`blocked`; ties go to the later `date`. `pass`, `fail` and `skipped` share a rank because each is a
statement about silicon, so a later `skipped` supersedes an earlier `pass` instead of being hidden by
it — if a case has decided a layer is unobservable, an old green run is the stale fact, not the new
skip. A build sweep never overwrites a hardware result, and a fresh hardware run always wins. The
consumer implements exactly this, and so does the collector.

A case that exists but has never run is reported as exactly that, which is the honest state — do not
leave a passing run unrecorded, and do not edit an old file to describe a new run.
