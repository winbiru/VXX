#!/usr/bin/env python3
"""Compare two V++ benchmark binaries using repeated same-machine samples."""

from __future__ import annotations

import argparse
import json
import statistics
import subprocess
import sys
from pathlib import Path


def parse_output(text: str) -> tuple[str, dict[str, float]]:
    metadata = ""
    cases: dict[str, float] = {}
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("benchmark_meta="):
            metadata = line
            continue
        if not line.startswith("benchmark="):
            continue
        fields = {}
        for item in line.split():
            if "=" in item:
                key, value = item.split("=", 1)
                fields[key] = value
        if "benchmark" not in fields or "ns_per_iteration" not in fields:
            raise ValueError(f"malformed benchmark line: {line}")
        cases[fields["benchmark"]] = float(fields["ns_per_iteration"])
    if not metadata or not cases:
        raise ValueError("benchmark output is missing metadata or cases")
    return metadata, cases


def run_binary(executable: Path, cwd: Path) -> tuple[str, dict[str, float]]:
    completed = subprocess.run(
        [str(executable)],
        cwd=str(cwd),
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return parse_output(completed.stdout)


def percentile95(values: list[float]) -> float:
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, int(round(0.95 * (len(ordered) - 1)))))
    return ordered[index]


def summarize(values: list[float]) -> dict[str, float]:
    median = statistics.median(values)
    mad = statistics.median(abs(value - median) for value in values)
    return {
        "median_ns": median,
        "p95_ns": percentile95(values),
        "mad_ns": mad,
        "mad_ratio": 0.0 if median == 0 else mad / median,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--cwd", type=Path, default=Path.cwd())
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--samples", type=int, default=40)
    parser.add_argument("--p95-min-samples", type=int, default=40)
    parser.add_argument("--time-threshold", type=float, default=0.10)
    parser.add_argument("--gc-threshold", type=float, default=0.15)
    parser.add_argument("--noise-threshold", type=float, default=0.05)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    if args.warmup < 0 or args.samples < 10:
        parser.error("warmup must be >= 0 and samples must be >= 10")
    if args.p95_min_samples < 20:
        parser.error("p95-min-samples must be >= 20")

    baseline = args.baseline.resolve()
    candidate = args.candidate.resolve()
    cwd = args.cwd.resolve()
    if not baseline.is_file() or not candidate.is_file():
        parser.error("baseline/candidate executable is missing")

    try:
        for _ in range(args.warmup):
            run_binary(baseline, cwd)
            run_binary(candidate, cwd)

        baseline_samples: dict[str, list[float]] = {}
        candidate_samples: dict[str, list[float]] = {}
        expected_meta = None
        expected_cases = None

        for _ in range(args.samples):
            for label, executable, destination in (
                ("baseline", baseline, baseline_samples),
                ("candidate", candidate, candidate_samples),
            ):
                metadata, cases = run_binary(executable, cwd)
                if expected_meta is None:
                    expected_meta = metadata
                    expected_cases = set(cases)
                elif metadata != expected_meta:
                    raise ValueError(
                        f"benchmark metadata mismatch for {label}: {metadata!r} != {expected_meta!r}"
                    )
                if set(cases) != expected_cases:
                    raise ValueError(f"benchmark case set mismatch for {label}")
                for name, value in cases.items():
                    destination.setdefault(name, []).append(value)
    except (subprocess.CalledProcessError, ValueError) as error:
        print(f"benchmark regression: INVALID: {error}", file=sys.stderr)
        return 2

    results = {}
    overall = "pass"
    for name in sorted(baseline_samples):
        before = summarize(baseline_samples[name])
        after = summarize(candidate_samples[name])
        median_delta = after["median_ns"] / before["median_ns"] - 1.0
        p95_delta = after["p95_ns"] / before["p95_ns"] - 1.0
        noisy = (
            before["mad_ratio"] > args.noise_threshold
            or after["mad_ratio"] > args.noise_threshold
        )
        threshold = args.gc_threshold if name.startswith("gc_") else args.time_threshold
        median_regressed = median_delta > threshold
        p95_regressed = name.startswith("gc_") and p95_delta > args.gc_threshold
        p95_reliable = args.samples >= args.p95_min_samples
        status = "pass"
        if noisy:
            status = "inconclusive"
            overall = "inconclusive" if overall == "pass" else overall
        elif median_regressed:
            status = "regression"
            overall = "regression"
        elif p95_regressed and not p95_reliable:
            status = "inconclusive"
            overall = "inconclusive" if overall == "pass" else overall
        elif p95_regressed:
            status = "regression"
            overall = "regression"
        results[name] = {
            "baseline": before,
            "candidate": after,
            "median_delta_ratio": median_delta,
            "p95_delta_ratio": p95_delta,
            "threshold_ratio": threshold,
            "p95_gate_reliable": p95_reliable,
            "status": status,
            "baseline_samples_ns": baseline_samples[name],
            "candidate_samples_ns": candidate_samples[name],
        }

    report = {
        "schema": 1,
        "metadata": expected_meta,
        "warmup": args.warmup,
        "samples": args.samples,
        "p95_min_samples": args.p95_min_samples,
        "time_threshold_ratio": args.time_threshold,
        "gc_threshold_ratio": args.gc_threshold,
        "noise_threshold_ratio": args.noise_threshold,
        "overall": overall,
        "cases": results,
    }
    rendered = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0 if overall == "pass" else (1 if overall == "regression" else 2)


if __name__ == "__main__":
    raise SystemExit(main())
