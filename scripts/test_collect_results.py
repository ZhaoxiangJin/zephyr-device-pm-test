# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0
"""Regression tests for result-only collection (standard library only)."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("collect_results.py")


class CollectResultsTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.outdir = self.root / "results"
        self.outdir.mkdir()
        self.report = {
            "environment": {
                "run_date": "2026-10-28T10:00:00",
                "zephyr_version": "tested-zephyr",
            },
            "testsuites": [],
        }

    def suite(self, layer, status):
        return {
            "name": f"drivers.pm.freqme.{layer}",
            "path": "tests/freqme",
            "platform": "frdm_mcxa153",
            "status": status,
        }

    def run_collector(self, *args):
        (self.root / "twister.json").write_text(json.dumps(self.report))
        return subprocess.run(
            [
                sys.executable, str(SCRIPT),
                "--twister-out", str(self.root),
                "--outdir", str(self.outdir),
                "--where", "local",
                "--harness", "tested-harness",
                *args,
            ],
            capture_output=True, text=True,
        )

    def record(self):
        paths = list(self.outdir.glob("*.json"))
        self.assertEqual(len(paths), 1)
        self.assertEqual(paths[0].name, "freqme.json")
        self.assertEqual(list(self.outdir.iterdir()), paths)
        return json.loads(paths[0].read_text())

    def test_outcomes_and_identifiers_only(self):
        statuses = {
            "baseline": ("passed", "pass"),
            "device": ("failed", "fail"),
            "runtime": ("error", "fail"),
            "system": ("skipped", "skipped"),
            "sysmanaged": ("not run", "built"),
            "dpd": ("blocked", "blocked"),
        }
        self.report["testsuites"] = [
            self.suite(layer, status) for layer, (status, _) in statuses.items()
        ] + [self.suite("baseline", "filtered")]
        result = self.run_collector(
            "--job", "frdm_mcxa153=123",
            "--station", "frdm_mcxa153=456",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.record(), {
            "run": "20261028-freqme-local",
            "date": "2026-10-28",
            "case": "freqme",
            "where": "local",
            "zephyr": "tested-zephyr",
            "harness": "tested-harness",
            "targets": {
                "frdm_mcxa153": {
                    "job": "123",
                    "station": "456",
                    "layers": {layer: verdict for layer, (_, verdict) in statuses.items()},
                },
            },
        })

    def test_target_can_be_blocked(self):
        self.report["testsuites"] = [self.suite("baseline", "notrun")]
        result = self.run_collector("--blocked", "frdm_mcxa153")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            self.record()["targets"]["frdm_mcxa153"]["layers"],
            {"baseline": "blocked"},
        )

    def test_single_layer_can_be_blocked(self):
        self.report["testsuites"] = [
            self.suite("baseline", "passed"),
            self.suite("dpd", "notrun"),
        ]
        result = self.run_collector("--blocked-layer", "dpd")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            self.record()["targets"]["frdm_mcxa153"]["layers"],
            {"baseline": "pass", "dpd": "blocked"},
        )

    def test_console_verdict_replaces_build_only(self):
        self.report["testsuites"] = [self.suite("baseline", "notrun")]
        console = self.root / "console.txt"
        for summary, expected in [
            ("PROJECT EXECUTION SUCCESSFUL", "pass"),
            ("PROJECT EXECUTION FAILED", "fail"),
        ]:
            with self.subTest(expected=expected):
                console.write_text(f"Running TESTSUITE freqme\n{summary}\n")
                result = self.run_collector("--console", f"baseline={console}")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(
                    self.record()["targets"]["frdm_mcxa153"]["layers"],
                    {"baseline": expected},
                )

    def test_empty_console_is_rejected(self):
        self.report["testsuites"] = [self.suite("baseline", "notrun")]
        console = self.root / "console.txt"
        console.write_text("")
        result = self.run_collector("--console", f"baseline={console}")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no ztest output", result.stderr)
        self.assertEqual(list(self.outdir.glob("*.json")), [])

    def test_process_notes_are_rejected(self):
        self.report["testsuites"] = [self.suite("baseline", "passed")]
        result = self.run_collector("--note", "frdm_mcxa153=retry history")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unrecognized arguments: --note", result.stderr)
        self.assertEqual(list(self.outdir.glob("*.json")), [])

    def test_unknown_status_is_rejected(self):
        self.report["testsuites"] = [self.suite("baseline", "unexpected")]
        result = self.run_collector()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unhandled twister status", result.stderr)
        self.assertEqual(list(self.outdir.glob("*.json")), [])

    def test_newer_result_replaces_existing_file(self):
        self.report["testsuites"] = [self.suite("baseline", "passed")]
        result = self.run_collector()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.report["environment"]["run_date"] = "2026-10-29T10:00:00"
        self.report["environment"]["zephyr_version"] = "new-zephyr"
        self.report["testsuites"] = [self.suite("baseline", "failed")]
        result = self.run_collector()
        self.assertEqual(result.returncode, 0, result.stderr)
        record = self.record()
        self.assertEqual(record["date"], "2026-10-29")
        self.assertEqual(record["zephyr"], "new-zephyr")
        self.assertEqual(
            record["targets"]["frdm_mcxa153"]["layers"],
            {"baseline": "fail"},
        )

    def test_same_day_result_replaces_coverage_without_merging(self):
        self.report["testsuites"] = [
            self.suite("baseline", "passed"),
            self.suite("device", "passed"),
        ]
        result = self.run_collector()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.report["testsuites"] = [self.suite("device", "failed")]
        result = self.run_collector("--where", "dapeng", "--tag", "latest")
        self.assertEqual(result.returncode, 0, result.stderr)
        record = self.record()
        self.assertEqual(record["run"], "20261028-freqme-dapeng-latest")
        self.assertEqual(record["where"], "dapeng")
        self.assertEqual(
            record["targets"]["frdm_mcxa153"]["layers"],
            {"device": "fail"},
        )

    def test_older_result_is_rejected(self):
        self.report["testsuites"] = [self.suite("baseline", "passed")]
        result = self.run_collector()
        self.assertEqual(result.returncode, 0, result.stderr)
        original = self.record()
        self.report["environment"]["run_date"] = "2026-10-27T10:00:00"
        self.report["testsuites"] = [self.suite("baseline", "failed")]
        result = self.run_collector()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("newer than", result.stderr)
        self.assertEqual(self.record(), original)

    def test_force_can_replace_with_older_result(self):
        self.report["testsuites"] = [self.suite("baseline", "passed")]
        result = self.run_collector()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.report["environment"]["run_date"] = "2026-10-27T10:00:00"
        self.report["testsuites"] = [self.suite("baseline", "failed")]
        result = self.run_collector("--force")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.record()["date"], "2026-10-27")
        self.assertEqual(
            self.record()["targets"]["frdm_mcxa153"]["layers"],
            {"baseline": "fail"},
        )

    def test_invalid_date_is_rejected(self):
        self.report["testsuites"] = [self.suite("baseline", "passed")]
        result = self.run_collector("--date", "2026-99-99")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid run date", result.stderr)
        self.assertEqual(list(self.outdir.iterdir()), [])

    def test_dry_run_does_not_replace_result(self):
        self.report["testsuites"] = [self.suite("baseline", "passed")]
        result = self.run_collector()
        self.assertEqual(result.returncode, 0, result.stderr)
        original = self.record()
        self.report["testsuites"] = [self.suite("baseline", "failed")]
        result = self.run_collector("--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("freqme.json", result.stdout)
        self.assertEqual(self.record(), original)

    def test_multiple_cases_have_one_file_each(self):
        other = self.suite("device", "passed")
        other.update(name="drivers.pm.mrt.device", path="tests/mrt")
        self.report["testsuites"] = [self.suite("baseline", "passed"), other]
        result = self.run_collector()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            {path.name for path in self.outdir.iterdir()},
            {"freqme.json", "mrt.json"},
        )
        for path in self.outdir.glob("*.json"):
            self.assertEqual(json.loads(path.read_text())["case"], path.stem)


if __name__ == "__main__":
    unittest.main()
