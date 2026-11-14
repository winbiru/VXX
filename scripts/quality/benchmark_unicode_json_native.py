#!/usr/bin/env python3
"""Measure matched native application binaries, including the pure V++ runtime."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import statistics
import subprocess

CASES = {
    "unicode_replace": 2048,
    "json_parse": 512,
    "arithmetic": 16777216,
    "branch_loop": 524288,
    "fibonacci": 1048576,
    "list_128": 262144,
}
LARGE_CASES = {
    "parse_nested": 4,
    "parse_long": 8,
    "parse_very_long": 2,
    "parse_huge": 1,
    "serialize_nested": 4,
    "serialize_long": 8,
    "serialize_very_long": 2,
}
EXPECTED = {
    "unicode_replace": [132, 158, 184, 106] * 4,
    "json_parse": list(range(419, 675, 17)),
}
LARGE_EXPECTED = {
    "parse_nested": 64,
    "parse_long": 384,
    "parse_very_long": 4096,
    "parse_huge": 16384,
    "serialize_long": len("[" + ",".join(map(str, range(384))) + "]"),
    "serialize_very_long": len("[" + ",".join(map(str, range(4096))) + "]"),
}

def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def provenance(root, workload):
    project = Path(__file__).resolve().parents[2]
    sources = (workload, "gói/dữ liệu/json/json.vi", "gói/máy ảo/host ABI.vi")
    hashes = {name: {
        "baseline": sha256(root / "baseline-tree" / name),
        "current": sha256(project / name),
    } for name in sources}
    if hashes[workload]["baseline"] != hashes[workload]["current"]:
        raise RuntimeError("Benchmark source differs between variants")
    return {
        "source_sha256": hashes,
        "runtime_sha256": {name: sha256(root / name / "runtime.o")
                           for name in ("runtime-base", "runtime-opt")},
    }

def report_path(root, stem, cases, all_cases):
    suffix = "" if list(cases) == list(all_cases) else "-" + "-".join(cases)
    return root / (stem + suffix + ".json")

def large_json(root, runs, cases):
    variants = {name: root / name for name in (
        "json-large-base-vpp", "json-large-opt-vpp",
        "json-large-base-cpp", "json-large-opt-cpp")}
    hashes = {name: sha256(path) for name, path in variants.items()}
    inputs = provenance(root, "benchmark/json_serialize_ab.vi")
    report = report_path(root, "native-large-ab", cases, LARGE_CASES)
    rng = random.Random(20261112)
    results = {}
    for case in cases:
        count = LARGE_CASES[case]
        names = list(variants)
        samples = {name: [] for name in names}
        expected = LARGE_EXPECTED.get(case)
        for _ in range(runs):
            rng.shuffle(names)
            for name in names:
                env = os.environ.copy()
                env.pop("VPP_VM_PROFILE", None)
                env.pop("BENCH_ARRAY_SIZE", None)
                env.update(BENCH_CASE=case, BENCH_ITERATIONS=str(count))
                run = subprocess.run([str(variants[name])], env=env,
                                     capture_output=True, text=True, timeout=120)
                if run.returncode:
                    raise RuntimeError(f"{case}/{name}: exit={run.returncode}: {run.stderr}{run.stdout}")
                elapsed, checksum, unit = map(float, run.stdout.strip().splitlines())
                if expected is None:
                    expected = unit
                if elapsed <= 0 or checksum != expected * count or unit != expected:
                    raise RuntimeError(f"Invalid result: {case}/{name}: {run.stdout}")
                samples[name].append(elapsed / count)
        results[case] = {"iterations": count, "unit": expected, "variants": {}}
        for name, values in samples.items():
            median = statistics.median(values)
            results[case]["variants"][name] = {
                "median_ms": median,
                "mad_ms": statistics.median(abs(x - median) for x in values),
                "samples_ms": values,
            }
        print(case, json.dumps(results[case]["variants"]), flush=True)
        report.write_text(json.dumps({
            **inputs, "binary_sha256": hashes, "runs": runs, "results": results,
        }, indent=2) + "\n")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--large", action="store_true")
    parser.add_argument("--large-cases", nargs="+", choices=LARGE_CASES, default=list(LARGE_CASES))
    parser.add_argument("--runs", type=int, default=7)
    parser.add_argument("--cases", nargs="+", choices=CASES, default=list(CASES))
    args = parser.parse_args()
    if args.runs < 3:
        parser.error("Use at least three rounds")
    root = args.directory.resolve()
    if args.large:
        return large_json(root, args.runs, args.large_cases)
    variants = {
        "baseline-vpp": root / "baseline-vpp",
        "json-opt-vpp": root / "json-opt-vpp",
        "runtime-opt-vpp": root / "runtime-opt-vpp",
        "combined-opt-vpp": root / "combined-opt-vpp",
        "baseline": root / "baseline",
        "json-opt-cpp": root / "json-opt-cpp",
    }
    hashes = {name: sha256(path) for name, path in variants.items()}
    inputs = provenance(root, "benchmark/languages/workloads.vi")
    report = report_path(root, "native-ab", args.cases, CASES)
    rng = random.Random(20261112)
    results = {}
    for case in args.cases:
        count = CASES[case]
        names = list(variants) if case in EXPECTED else ["baseline-vpp", "combined-opt-vpp"]
        samples = {name: [] for name in names}
        checks = EXPECTED.get(case)
        for _ in range(args.runs):
            rng.shuffle(names)
            for name in names:
                env = os.environ.copy()
                env.pop("VPP_VM_PROFILE", None)
                env.update(BENCH_CASE=case, BENCH_ITERATIONS=str(count), BENCH_SEED="17")
                run = subprocess.run([str(variants[name])], env=env,
                                     capture_output=True, text=True, timeout=120)
                if run.returncode:
                    raise RuntimeError(f"{case}/{name}: exit={run.returncode}: {run.stderr}{run.stdout}")
                data = json.loads(run.stdout)
                if checks is None:
                    checks = data["checks"]
                expected = sum(x % 997 for x in checks) * (count // 16)
                if (data["case"] != case or data["iterations"] != count or
                    data["seed"] != 17 or data["checks"] != checks or len(checks) != 16 or
                    int(data["checksum"]) != expected or data["ns_per_op"] <= 0):
                    raise RuntimeError(f"Invalid result: {case}/{name}: {data}")
                samples[name].append(data["ns_per_op"])
        results[case] = {"iterations": count, "checks": checks, "variants": {}}
        for name, values in samples.items():
            median = statistics.median(values)
            results[case]["variants"][name] = {
                "median_ns": median,
                "mad_ns": statistics.median(abs(x - median) for x in values),
                "samples_ns": values,
            }
        print(case, json.dumps(results[case]["variants"]), flush=True)
        report.write_text(json.dumps({
            **inputs, "binary_sha256": hashes,
            "runs": args.runs, "results": results,
        }, indent=2) + "\n")

if __name__ == "__main__":
    main()
