#!/usr/bin/env python3
# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0
"""Turn a twister report into a run record under results/.

The verdict per layer comes from twister's own twister.json, never from a human
reading a console log. Everything this script writes is a machine fact; the
argument about what a failure *means* belongs in docs/findings.md, and the
provenance a later reader cannot reconstruct -- which station, why a target was
unreachable -- is what the --note and --blocked options are for.

Standard library only: this runs on a farm image with no pip.
"""

from __future__ import annotations

import argparse
import datetime
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# A twister status is a statement about one image on one board. A verdict is a
# statement about a layer, and docs/results.md defines the vocabulary.
VERDICT_OF_STATUS = {
    "passed": "pass",
    "failed": "fail",
    # A build failure is a real defect -- the LPADC multi-instance PM_DEVICE
    # defect was exactly this -- so it is a fail, not an absence of evidence.
    "error": "fail",
    "skipped": "skipped",
    "not run": "built",
    "notrun": "built",
    "blocked": "blocked",
}

# "It ran on the board" outranks "it compiled", which outranks "it never got
# there". Within a rank the later date wins, so a fresh skip supersedes an old
# pass rather than being hidden by it.
VERDICT_RANK = {"blocked": 0, "built": 1, "skipped": 2, "fail": 3, "pass": 3}


def known_layers() -> set[str]:
    """The layers that exist, read from the snippets rather than a list here.

    An unknown layer means a scenario name that no snippet backs, which is how a
    scenario ends up declaring a layer the report cannot join to anything.
    """
    return {"baseline"} | {p.name[len("pm-"):] for p in (REPO / "snippets").glob("pm-*")}


def git_describe(path: Path) -> str:
    """The commit a run is evidence about, marked -dirty when it is not the whole story.

    A record whose `harness` names a commit that does not contain the code that
    ran is worse than one with no commit at all, because it looks joinable.
    """
    try:
        head = subprocess.run(
            ["git", "-C", str(path), "rev-parse", "--short", "HEAD"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
        dirty = subprocess.run(
            # results/ is excluded: a record is evidence about a run, not code that
            # took part in one, so an uncommitted record from an earlier run says
            # nothing about whether this commit is what ran.
            ["git", "-C", str(path), "status", "--porcelain", "--", ":(exclude)results"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "unknown"
    if dirty:
        print(f"warning: {path.name} has uncommitted changes, so {head} is not what ran. "
              f"Recording {head}-dirty; commit and rerun --harness to fix the provenance.",
              file=sys.stderr)
        return f"{head}-dirty"
    return head


def parse_console(path: Path) -> str:
    """The verdict a ztest console capture states, or an exit if it states nothing.

    A farm run flashes images twister only built, so the verdict has to come from
    the board's own summary rather than from twister's status. ztest prints one,
    which is why the cases were migrated to it: this reads a machine's line, not a
    human's reading of a log.

    A capture with no ztest output at all is not evidence about the board -- a
    console that was never carrying the board's bytes looks exactly like a board
    that never booted -- so it is refused rather than recorded as a failure.
    """
    text = path.read_text(errors="replace")
    if "Running TESTSUITE" not in text:
        sys.exit(f"{path}: no ztest output, so this capture says nothing about the "
                 f"board. Check the console really was attached before the reset.")
    if "PROJECT EXECUTION SUCCESSFUL" in text:
        return "pass"
    # Either ztest said so, or it started and never finished. Both are "did not
    # pass"; which one it was belongs in docs/findings.md, not in the verdict.
    return "fail"


def parse_assignments(pairs: list[str], what: str) -> dict[str, str]:
    out = {}
    for pair in pairs:
        target, sep, value = pair.partition("=")
        if not sep or not target or not value:
            sys.exit(f"--{what} wants TARGET=VALUE, got {pair!r}")
        out[target] = value
    return out


def collect(report: dict, layers: set[str]) -> tuple[dict, list[str]]:
    """Fold twister's flat suite list into {case: {target: {layer: verdict}}}."""
    cases: dict[str, dict[str, dict[str, str]]] = {}
    complaints = []
    filtered = 0

    for suite in report["testsuites"]:
        status = suite.get("status")
        if status == "filtered":
            # Not recorded: twister decided the scenario does not apply here. It
            # is still worth counting out loud, because a snippet that cannot be
            # resolved also arrives as a filter and would otherwise look like a
            # green run over a fraction of the matrix.
            filtered += 1
            continue

        layer = suite["name"].rsplit(".", 1)[-1]
        if layer not in layers:
            complaints.append(
                f"scenario {suite['name']!r} ends in {layer!r}, which no snippet backs"
            )
            continue

        verdict = VERDICT_OF_STATUS.get(status)
        if verdict is None:
            complaints.append(f"unhandled twister status {status!r} on {suite['name']}")
            continue

        case = Path(suite["path"]).name
        target = suite["platform"]
        existing = cases.setdefault(case, {}).setdefault(target, {}).get(layer)
        if existing is None or VERDICT_RANK[verdict] >= VERDICT_RANK[existing]:
            cases[case][target][layer] = verdict

    if filtered:
        print(f"note: {filtered} configurations were filtered and are not recorded",
              file=sys.stderr)
    return cases, complaints


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--twister-out", required=True, type=Path,
                    help="twister output directory holding twister.json")
    ap.add_argument("--where", required=True,
                    help="how the run happened: dapeng, local, build-sweep")
    ap.add_argument("--tag", help="suffix for the file name, e.g. pd-guard")
    ap.add_argument("--date", help="YYYY-MM-DD; defaults to the twister run date")
    ap.add_argument("--zephyr", help="override the Zephyr commit twister reported")
    ap.add_argument("--harness", help="override this repository's commit")
    ap.add_argument("--job", action="append", default=[], metavar="TARGET=ID")
    ap.add_argument("--station", action="append", default=[], metavar="TARGET=ID")
    ap.add_argument("--note", action="append", default=[], metavar="TARGET=TEXT",
                    help="run provenance only; never analysis")
    ap.add_argument("--blocked", action="append", default=[], metavar="TARGET",
                    help="target that never reached a board; every layer is recorded blocked")
    ap.add_argument("--console", action="append", default=[], metavar="LAYER=FILE",
                    help="ztest capture from a board, for layers this twister run "
                         "only built; the board's verdict replaces \"built\"")
    ap.add_argument("--case", action="append", default=[],
                    help="only write these cases; default is every case in the report")
    ap.add_argument("--outdir", type=Path, default=REPO / "results")
    ap.add_argument("--force", action="store_true",
                    help="overwrite an existing file for this date/case/where")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    report_path = args.twister_out / "twister.json"
    if not report_path.is_file():
        return f"no twister.json under {args.twister_out}"
    report = json.loads(report_path.read_text())

    layers = known_layers()
    cases, complaints = collect(report, layers)
    for complaint in complaints:
        print(f"error: {complaint}", file=sys.stderr)
    if complaints:
        return "the report does not join to this repository's layers; nothing written"

    if args.console:
        # The board's verdict, joined to the build that produced the image: the
        # target spelling and the Zephyr commit still come from twister, so a farm
        # record joins to a build record instead of standing beside it.
        captures = parse_assignments(args.console, "console")
        seen = {target for case in cases.values() for target in case}
        if len(seen) != 1:
            return (f"--console describes one board, but this report covers {len(seen)} "
                    f"targets; narrow the twister run with -p")
        target = seen.pop()
        for layer, capture in sorted(captures.items()):
            if layer not in layers:
                return f"--console names layer {layer!r}, which no snippet backs"
            verdict = parse_console(Path(capture))
            for found in cases.values():
                if layer not in found[target]:
                    return (f"--console names layer {layer!r}, which this run did not "
                            f"build for {target}")
                found[target][layer] = verdict

    env = report.get("environment", {})
    date = args.date or (env.get("run_date") or "")[:10] \
        or datetime.date.today().isoformat()
    zephyr = args.zephyr or env.get("zephyr_version", "unknown")
    harness = args.harness or git_describe(REPO)

    jobs = parse_assignments(args.job, "job")
    stations = parse_assignments(args.station, "station")
    notes = parse_assignments(args.note, "note")

    wanted = set(args.case) or set(cases)
    unknown = wanted - set(cases)
    if unknown:
        return f"no results for case(s) {', '.join(sorted(unknown))}"

    for case in sorted(wanted):
        targets = {}
        for target, found in sorted(cases[case].items()):
            if target in args.blocked:
                found = {layer: "blocked" for layer in found}
            entry = {}
            for key, table in (("job", jobs), ("station", stations), ("note", notes)):
                if target in table:
                    entry[key] = table[target]
            entry["layers"] = dict(sorted(found.items()))
            targets[target] = entry

        stem = "-".join(filter(None, [date.replace("-", ""), case, args.where, args.tag]))
        record = {
            "run": stem,
            "date": date,
            "case": case,
            "where": args.where,
            "zephyr": zephyr,
            "harness": harness,
            "targets": targets,
        }
        text = json.dumps(record, indent=2) + "\n"
        path = args.outdir / f"{stem}.json"

        if args.dry_run:
            print(f"--- {path}")
            print(text, end="")
            continue
        if path.exists() and not args.force:
            return (f"{path.name} exists. A new run is a new file -- pass --tag to name "
                    f"this one, or --force if you are correcting the same run")
        path.write_text(text, newline="\n")
        tally = {}
        for entry in targets.values():
            for verdict in entry["layers"].values():
                tally[verdict] = tally.get(verdict, 0) + 1
        summary = ", ".join(f"{n} {v}" for v, n in sorted(tally.items()))
        print(f"{path.name}: {len(targets)} targets, {summary}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
