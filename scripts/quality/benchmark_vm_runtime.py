#!/usr/bin/env python3
"""Compare two standalone VMs on identical prebuilt bytecode and expected output."""
import argparse
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time

from benchmark_vm_allocations import COUNTERS, identity


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("cases", nargs="+", metavar="FIXTURE=VMB")
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error("--repeats must be positive")
    root = args.root.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    vms = {"baseline": args.baseline.resolve(), "candidate": args.candidate.resolve()}
    env = dict(os.environ, VPP_HOME=str(root))
    env.pop("VPP_VM_PROFILE", None)
    env.pop("VPP_AOT_CPU_PROFILE", None)
    report = {"platform": platform.platform(), "machine": platform.machine(),
              "vms": {label: identity(path) for label, path in vms.items()},
              "repeats": args.repeats, "results": [], "verified": False}
    for case in args.cases:
        name, sep, image_path = case.partition("=")
        if not sep or not name or Path(name).name != name:
            parser.error(f"Invalid case: {case}")
        image = Path(image_path).resolve()
        expected = root / "scripts/test/expected" / f"{name}.expected"
        expected_output = expected.read_bytes()
        row = {"fixture": name, "image": identity(image), "expected": identity(expected)}

        def run(label, profile):
            run_env = dict(env, VPP_VM_PROFILE="1") if profile else env
            start = time.perf_counter()
            result = subprocess.run([str(vms[label]), str(image)], cwd=root,
                                    env=run_env, capture_output=True, timeout=args.timeout)
            elapsed = time.perf_counter() - start
            if profile or result.returncode or result.stdout != expected_output:
                prefix = output / f"{name}-{label}.{'profile' if profile else 'failure'}"
                Path(str(prefix) + ".stdout").write_bytes(result.stdout)
                Path(str(prefix) + ".stderr").write_bytes(result.stderr)
            if result.returncode or result.stdout != expected_output:
                raise RuntimeError(f"{name}/{label}: status {result.returncode} or output mismatch")
            return result, elapsed

        for label in vms:
            result, _ = run(label, True)
            counters = COUNTERS.findall(result.stderr)
            if len(counters) != 1:
                raise RuntimeError(f"{name}/{label}: missing allocation counters")
            row[label] = dict(zip(("allocations", "collections", "free_probes"),
                                  map(int, counters[0])))
            row[label]["seconds"] = []
            run(label, False)  # warm up each executable before alternating timings
        for index in range(args.repeats):
            order = tuple(vms) if index % 2 == 0 else tuple(reversed(vms))
            for label in order:
                _, elapsed = run(label, False)
                row[label]["seconds"].append(elapsed)
        for label in vms:
            row[label]["median_seconds"] = statistics.median(row[label]["seconds"])
        if identity(image) != row["image"] or identity(expected) != row["expected"]:
            raise RuntimeError(f"{name}: input changed during comparison")
        report["results"].append(row)
        print(name, {label: row[label] for label in vms}, flush=True)
    for label, path in vms.items():
        if identity(path) != report["vms"][label]:
            raise RuntimeError(f"{label}: executable changed during comparison")
    report["verified"] = True
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
