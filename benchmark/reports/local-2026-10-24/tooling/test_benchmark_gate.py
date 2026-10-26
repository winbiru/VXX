#!/usr/bin/env python3
"""Correctness tests for release decisions and malformed benchmark evidence."""

import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from compare_benchmarks import InvalidReport, compare, percentile


def fixture(count=40, gc=True):
    name = "gc_burst_collect_reuse" if gc else "vm_verify"
    meta = {"meta": "harness-v3", "build": "Release", "coverage": "OFF", "flags": "-O3 -DNDEBUG",
            "compiler": "test compiler", "gc_policy": "explicit"}
    build = {"meta": meta, "cmake": {"CMAKE_BUILD_TYPE": "Release"}, "binary_sha256": "binary"}
    report = {"schema": 1, "protocol": "alternating-independent-processes-v1",
              "system": {"host": "same-host"}, "environment": {"VPP_ENABLE_JIT": "0"},
              "harness_sha256": "harness", "sample_count": count, "warmup_count": 3,
              "sources": {side: {"commit": side, "snapshot_sha256": side}
                          for side in ("baseline", "candidate")},
              "builds": {side: copy.deepcopy(build) for side in ("baseline", "candidate")},
              "budget": {"time": .10, "rss": .15, "gc_p95": .15, "allocations": .15, "noise": .05},
              "plan": [{"case": name, "iterations": 2}], "samples": []}
    for phase, samples in (("warmup", 3), ("measure", count)):
        for pair in range(samples):
            order = ("baseline", "candidate") if pair % 2 == 0 else ("candidate", "baseline")
            for position, side in enumerate(order):
                record = {"case": name, "iterations": 2, "checksum": 123,
                          "ns_per_iteration": 100, "peak_rss_bytes": 10000}
                if gc:
                    record.update(gc_pauses_ns=[80, 100], allocation_count=6000, allocation_bytes=200000,
                                  gc_allocation_count=30, gc_allocation_bytes=15000,
                                  managed_allocations=1024, stack_resize_count=0, stack_trim_count=0,
                                  tracked_after=1, swept=1024)
                report["samples"].append({"side": side, "phase": phase, "pair": pair,
                                          "order": position, "record": record})
    return report


def change_candidate(report, field, value):
    for row in report["samples"]:
        if row["side"] == "candidate" and row["phase"] == "measure":
            row["record"][field] = value


class GateTests(unittest.TestCase):
    def test_pass(self):
        self.assertEqual(compare(fixture())["status"], "PASS")

    def test_time_regression(self):
        data = fixture()
        change_candidate(data, "ns_per_iteration", 111)
        self.assertEqual(compare(data)["status"], "FAIL")

    def test_rss_regression(self):
        data = fixture()
        change_candidate(data, "peak_rss_bytes", 11600)
        self.assertEqual(compare(data)["status"], "FAIL")

    def test_gc_tail_regression_even_if_total_time_unchanged(self):
        data = fixture()
        change_candidate(data, "gc_pauses_ns", [80, 140])
        self.assertEqual(compare(data)["status"], "FAIL")

    def test_allocation_regression(self):
        data = fixture()
        change_candidate(data, "allocation_count", 7000)
        self.assertEqual(compare(data)["status"], "FAIL")

    def test_not_enough_independent_gc_samples(self):
        self.assertEqual(compare(fixture(10))["status"], "INCONCLUSIVE")
        self.assertEqual(compare(fixture(10, gc=False))["status"], "PASS")

    def test_dispersion_is_not_pass(self):
        data = fixture()
        for row in data["samples"]:
            if row["side"] == "candidate" and row["phase"] == "measure":
                row["record"]["ns_per_iteration"] = 90 if row["pair"] % 2 else 110
        self.assertEqual(compare(data)["status"], "INCONCLUSIVE")

    def test_rejects_missing_duplicate_and_unknown_samples(self):
        for edit in (lambda data: data["samples"].pop(),
                     lambda data: data["samples"].append(data["samples"][0]),
                     lambda data: data["samples"][0]["record"].update(case="unexpected")):
            data = fixture()
            edit(data)
            with self.assertRaises(InvalidReport):
                compare(data)

    def test_rejects_build_and_metadata_mismatch(self):
        for field, value in (("compiler", "other compiler"), ("build", "Debug"),
                             ("flags", "-O3 -fsanitize=address"), ("coverage", "ON")):
            data = fixture()
            data["builds"]["candidate"]["meta"][field] = value
            with self.assertRaises(InvalidReport):
                compare(data)

    def test_rejects_invalid_numbers(self):
        for value in (0, -1, float("nan"), float("inf"), "100", True):
            data = fixture()
            data["samples"][0]["record"]["ns_per_iteration"] = value
            with self.assertRaises(InvalidReport):
                compare(data)

    def test_checksums_iterations_and_gc_invariants(self):
        for field, value in (("checksum", 999), ("iterations", 1), ("gc_pauses_ns", [1]),
                             ("tracked_after", 0), ("swept", 0), ("stack_trim_count", 1)):
            data = fixture()
            data["samples"][0]["record"][field] = value
            with self.assertRaises(InvalidReport):
                compare(data)

    def test_rejects_non_alternating_order(self):
        data = fixture()
        data["samples"][2]["order"] = 1
        with self.assertRaises(InvalidReport):
            compare(data)

    def test_percentile_interpolation(self):
        self.assertAlmostEqual(percentile([0, 10, 20, 30]), 28.5)

    def test_cli_exit_status_for_pass_fail_inconclusive_invalid(self):
        script = Path(__file__).with_name("compare_benchmarks.py")
        data = fixture()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "report.json"
            for report, code in ((data, 0), (fixture(10), 2), ({}, 3)):
                path.write_text(json.dumps(report))
                result = subprocess.run([sys.executable, str(script), str(path)], capture_output=True)
                self.assertEqual(result.returncode, code, result.stdout)
            change_candidate(data, "ns_per_iteration", 200)
            path.write_text(json.dumps(data))
            self.assertEqual(subprocess.run([sys.executable, str(script), str(path)],
                                            capture_output=True).returncode, 1)


if __name__ == "__main__":
    unittest.main()
