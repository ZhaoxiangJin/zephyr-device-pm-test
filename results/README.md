# Recorded test results

A `testcase.yaml` says which cases *exist*; these files say which ones have
actually been **run**, and where. The Device PM report in
[zephyr-data](https://github.com/ZhaoxiangJin/zephyr-data) reads both, so a
board/IP pair can be reported as "there is a test" separately from "the test
passes on silicon".

One file per run, named `<YYYYMMDD>-<sample>-<what>.json`. Never edit an old
file to record a new run: add another one. A later run supersedes an earlier
one per layer, so history stays readable and a regression is a diff.

```json
{
  "run": "20260912-lpadc-dapeng",
  "date": "2026-09-12",
  "sample": "lpadc",
  "where": "dapeng",
  "note": "free text about the run as a whole",
  "targets": {
    "frdm_mcxn947/mcxn947/cpu0": {
      "job": "2307577",
      "station": "1166@main",
      "note": "free text about this target",
      "layers": {"baseline": "pass", "device": "pass", "runtime": "pass", "system": "pass"}
    }
  }
}
```

- `sample` matches the directory under `samples/`, which is what ties the run to
  a DT `compatible` through that sample's `device-pm-case.json`.
- `targets` keys are board targets in the same slash form as `platform_allow`,
  so a typo shows up as an unmatched target in the report rather than silently
  counting as untested.
- `layers` keys are the case suffixes from `testcase.yaml`'s `tests:` block
  (`baseline`, `device`, `runtime`, `system`).
- Layer results:
  - `pass` — the console capture matched the PASS regex.
  - `fail` — it ran and did not pass. This is the one that must never be
    quietly dropped: it means a real driver defect.
  - `blocked` — never reached the board (no station, bridge would not come up).
    Says nothing about the driver.
  - `built` — the image compiles; it was not run. The weakest useful evidence.
- `job` / `station` are optional provenance. For a local run use
  `"where": "local"` and put the probe or serial port in `note`.

Ranking when several runs cover the same layer: `pass`/`fail` beat `built`,
which beats `blocked`; ties go to the later `date`. So a build sweep never
overwrites a hardware result, and a fresh hardware run always wins.
