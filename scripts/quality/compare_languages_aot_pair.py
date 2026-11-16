#!/usr/bin/env python3
"""Paired A/B measurement of two builds of the same V++ workload driver."""

import argparse
import hashlib
import json
import os
import platform
import random
import statistics
import subprocess
from pathlib import Path

from compare_languages_aot import ALL_CASES, MAX_ITERATIONS, checksum, expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=7)
    parser.add_argument("--case", choices=ALL_CASES, action="append")
    args = parser.parse_args()
    cases = args.case or ["unicode_replace", "unicode_normalize", "json_parse",
                          "json_encode", "json_nested", "list_128"]
    if args.runs < 3 or len(set(cases)) != len(cases):
        parser.error("Require at least three rounds and distinct cases")
    root = Path(__file__).resolve().parents[2]
    binaries = {"before": args.before.resolve(), "after": args.after.resolve()}
    env = dict(os.environ, VPP_HOME=str(root), BENCH_SEED="17")
    for key in ("VPP_VM_PROFILE", "VPP_AOT_CPU_PROFILE", "VPP_ENABLE_JIT",
                "VPP_ENABLE_MACHINE_JIT", "VIETVM_ENABLE_JIT"):
        env.pop(key, None)

    def run(name, case, n):
        p = subprocess.run([str(binaries[name])], env=dict(env, BENCH_CASE=case,
                           BENCH_ITERATIONS=str(n)), capture_output=True, text=True,
                           timeout=120)
        if p.returncode:
            raise RuntimeError(f"{name}/{case}: exit {p.returncode}\n{p.stderr}\n{p.stdout}")
        row = json.loads(p.stdout)
        if (row["case"] != case or row["iterations"] != n or row["seed"] != 17
                or row["checks"] != [expected(case, 17 + i) for i in range(16)]
                or int(row["checksum"]) != checksum(case, n)
                or row["elapsed_ms"] <= 0 or len(row["warmup_ms"]) != 3
                or not all(ms > 0 for ms in row["warmup_ms"])):
            raise ValueError(f"Invalid benchmark output: {name}/{case}: {row}")
        elapsed_ns = row["elapsed_ms"] * 1_000_000 / n
        if abs(row["ns_per_op"] - elapsed_ns) > max(0.01, elapsed_ns * 1e-6):
            raise ValueError(f"Invalid ns/op: {name}/{case}")
        return row

    counts = {}
    for case in cases:
        n = 16
        for _ in range(6):
            ms = run("before", case, n)["elapsed_ms"]
            if ms >= 50 or n >= MAX_ITERATIONS:
                break
            n = min(MAX_ITERATIONS, max(n * 2, int(n * 80 / ms / 16) * 16))
        counts[case] = n
        print(f"pilot {case} {n}", flush=True)

    rows = []
    rng = random.Random(20261114)
    for i in range(args.runs):
        order = cases[:]
        rng.shuffle(order)
        for case in order:
            pair = list(binaries)
            rng.shuffle(pair)
            for name in pair:
                rows.append(dict(binary=name, run=i, **run(name, case, counts[case])))
        print(f"round {i + 1}/{args.runs}", flush=True)

    summary = {}
    for case in cases:
        stats = {}
        for name in binaries:
            values = [r["ns_per_op"] for r in rows if r["case"] == case and r["binary"] == name]
            median = statistics.median(values)
            stats[name] = (median, statistics.median(abs(x - median) for x in values))
        a, a_mad = stats["before"]
        b, b_mad = stats["after"]
        summary[case] = dict(before_ns=a, after_ns=b, before_mad_ns=a_mad, after_mad_ns=b_mad,
                             reduction_pct=100 * (1 - b / a), speedup=a / b)
    report = dict(platform=platform.platform(), machine=platform.machine(), seed=17,
                  rounds=args.runs, binaries={name: dict(path=str(path), sha256=hashlib.sha256(
                      path.read_bytes()).hexdigest()) for name, path in binaries.items()},
                  counts=counts, summary=summary, samples=rows)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
