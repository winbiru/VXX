#!/usr/bin/env python3
"""Compare already-built executables; include process startup, exclude compilation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--warmup", type=int, default=3)
    args = parser.parse_args()
    if args.runs < 2 or args.warmup < 0:
        parser.error("runs must be >= 2 and warmup >= 0")
    paths = {"baseline": args.baseline.resolve(), "candidate": args.candidate.resolve()}
    env = os.environ.copy()
    env.pop("VPP_AOT_CPU_PROFILE", None)
    reference = None
    samples = {key: [] for key in paths}
    records = []
    for iteration in range(-args.warmup, args.runs):
        order = ["baseline", "candidate"]
        if iteration % 2:
            order.reverse()
        for key in order:
            start = time.perf_counter_ns()
            result = subprocess.run([str(paths[key])], env=env, capture_output=True, timeout=60)
            elapsed = (time.perf_counter_ns() - start) / 1e6
            if result.returncode:
                raise RuntimeError(f"{key} failed ({result.returncode}): {result.stderr.decode(errors='replace')}")
            if reference is None:
                reference = result.stdout
            if result.stdout != reference:
                raise RuntimeError(f"output mismatch: {key}")
            if iteration >= 0:
                samples[key].append(elapsed)
                records.append({"iteration": iteration, "variant": key, "wall_ms": elapsed})
    summary = {}
    for key, values in samples.items():
        median = statistics.median(values)
        summary[key] = {"median_ms": median,
                        "mad_ms": statistics.median(abs(x - median) for x in values),
                        "binary_sha256": hashlib.sha256(paths[key].read_bytes()).hexdigest(),
                        "binary_bytes": paths[key].stat().st_size}
    report = {"scope": "executable wall time including process startup; compilation excluded",
              "platform": platform.platform(), "machine": platform.machine(),
              "runs": args.runs, "warmup": args.warmup,
              "stdout_sha256": hashlib.sha256(reference).hexdigest(),
              "summary": summary, "samples": records,
              "speedup": summary["baseline"]["median_ms"] / summary["candidate"]["median_ms"]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"summary": summary, "speedup": report["speedup"]}, indent=2))


if __name__ == "__main__":
    main()
