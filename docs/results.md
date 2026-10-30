# Result records

A `testcase.yaml` says which layers *exist*. Each `results/<case>.json` contains that driver's
latest collected result: which layers ran, on which boards, and how. The Device PM report in
[zephyr-data](https://github.com/ZhaoxiangJin/zephyr-data) reads both, so a board/IP pair can be
reported as "there is a test" separately from "the test passes on silicon" — a distinction no amount
of static analysis of the Zephyr tree can make.

**These files hold results only:** per-target layer verdicts, tested revisions, result identifiers,
and optional final test counts or image hashes. Do not record build/debug steps, intermediate
candidates, retry histories, session cleanup, or development progress. A defect belongs in
[findings.md](findings.md); test requirements and coverage limits belong in the case's README.

## Producing one

```sh
python scripts/collect_results.py --twister-out /c/tw --where dapeng \
    --job frdm_mcxn947/mcxn947/cpu0=2309422 --station frdm_mcxn947/mcxn947/cpu0=1166@main
```

The verdict per layer comes from twister's own `twister.json`, not from a human reading a console
log. Targets that never reached a board are passed in with `--blocked <target>` and recorded as
such. A single layer that did reach the board but could not return a verdict there, for a reason
outside the driver under test, is `--blocked-layer <layer>`: it was run, so `built` would
understate it, and `fail` would blame the driver for someone else's defect.

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

Keep exactly one file per driver case, named `<case>.json`, such as `freqme.json`.
Collecting a newer result replaces that file, including its revisions and coverage; do not merge
targets or layers from older runs into it. A partial run therefore reports only its own coverage.
The `date`, `run` and source fields identify the retained result. `--tag` changes the run
identifier, not the filename.

An older date is rejected unless `--force` is explicitly supplied. For the same date, the last
collected result replaces the previous one. `--dry-run` previews the replacement without writing.
Previous results belong in Git history, not in additional files under `results/`.

A broad build-only job and a representative hardware job from the same campaign
may share this file only when both Zephyr and test-source revisions are identical.
Targets selected only for compilation remain `built`; attempted hardware targets
blocked in environment preparation remain `blocked`, not driver `fail`.

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
- `job` / `station` identify the source of a result. Optional `passed_tests`, `elf_sha256` and
  `serial_logs` mappings contain final per-layer counts or evidence references, not execution steps.
  The collector emits the core schema shown above.
- Optional `stations` maps layers to station IDs when a target ran on multiple stations.
  In that case, omit the single `station` field rather than attributing every layer to one station.
  `task_ids` and `run_ids` may identify each layer's final task and compiled/captured RunID.
  These mappings describe retained final results, not earlier attempts.
- Optional `build_layers` preserves verified build outcomes separately from hardware verdicts.
  `blocked_by` identifies an external blocker per layer, and `logs` links supporting farm logs.
  A farm setup failure is `blocked`, not a driver `fail` or a hardware `pass`.

## Verdicts

| Verdict | Means |
| --- | --- |
| `pass` | it ran on the board and passed |
| `fail` | it did not pass — it either ran and failed, or did not build. **The one that must never be quietly dropped**: it means a real defect, and that defect gets a row in [findings.md](findings.md) |
| `skipped` | it ran and the case decided the layer is not observable on this SoC, e.g. a peripheral the reference manual says keeps running through the state under test. A statement about silicon, not an absence of one |
| `blocked` | never reached the board — no station, bridge would not come up — or reached it and was stopped there by a defect outside the driver under test, such as `mcxa-suspend-to-idle-never-wakes` or `pd-soc-state-change-null-deref`. Says nothing about the driver |
| `built` | the image compiles; it was not run. The weakest useful evidence |

When duplicate suites within one Twister report cover the same target and layer, the collector
ranks `pass`/`fail` above `skipped`, then `built`, then `blocked`; equal ranks take the later suite.
This only folds a single report. Replacing a driver's result does not rank or merge older evidence:
even a build-only run replaces the previous snapshot when it is the latest collected result.

A case that exists but has never run is reported as exactly that, which is the honest state — do not
leave a passing run unrecorded. Update that case's single result file for a new run. Documentation
may show a final result table and link its JSON; omit job-progress summaries and rerun narratives.

## Collector checks

Run the standard-library regression tests from the repository root:

```sh
python -m unittest discover -s scripts -p "test_*.py"
```
