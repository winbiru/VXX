#!/usr/bin/env python3
"""Bounded, repeatable Stage 2 VM/ASM compiler profiling (stdlib only)."""

import argparse
import collections
import json
import os
from pathlib import Path
import re
import subprocess
import time


SAMPLE = re.compile(
    r"vm-profile: steps=(\d+) function=(\d+) pc=(\d+) "
    r"allocations=(\d+) gc=(\d+) free-list-probes=(\d+)"
)


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=float, default=45)
    parser.add_argument("--name", default="current", help="label for profiling artifacts")
    parser.add_argument("--vm", type=Path, default=root / "build/vm-asm-optimized/vpp-vm")
    parser.add_argument("--seed", type=Path, default=root / "build/vm-asm-selfhost/compiler-stage1.vmb")
    parser.add_argument("--source", type=Path, default=root / "gói/biên dịch/dòng lệnh VM.vi")
    parser.add_argument("--output-dir", type=Path, default=root / "build/vm-asm-optimized")
    args = parser.parse_args()
    if args.seconds <= 0 or not re.fullmatch(r"[a-zA-Z0-9_-]+", args.name):
        parser.error("--seconds must be positive and --name must contain only letters, digits, _ or -")

    outdir = args.output_dir.resolve()
    outdir.mkdir(parents=True, exist_ok=True)
    prefix = outdir / f"profile-stage2-{args.name}"
    target = outdir / f"compiler-stage2-{args.name}.vmb"
    command = [str(args.vm.resolve()), str(args.seed.resolve()), "build",
               str(args.source.resolve()), "-o", str(target)]
    env = os.environ.copy()
    env["VPP_VM_PROFILE"] = "1"
    env["VPP_HOME"] = str(root)
    start = time.monotonic()
    with prefix.with_suffix(".stdout").open("wb") as stdout, prefix.with_suffix(".log").open("wb") as stderr:
        try:
            completed = subprocess.run(command, cwd=root, env=env, stdout=stdout,
                                       stderr=stderr, timeout=args.seconds, check=False)
            result = "success" if completed.returncode == 0 else f"exit:{completed.returncode}"
        except subprocess.TimeoutExpired:
            result = "timeout"

    samples = SAMPLE.findall(prefix.with_suffix(".log").read_text(errors="replace"))
    sample_counts = collections.Counter(int(row[1]) for row in samples)
    last = samples[-1] if samples else None
    report = {
        "status": result,
        "seconds": round(time.monotonic() - start, 2),
        "samples": len(samples),
        "guest_instructions_observed": sum(int(row[0]) for row in samples),
        "allocations": int(last[3]) if last else None,
        "gc_collections": int(last[4]) if last else None,
        "free_list_probes": int(last[5]) if last else None,
        "top_function_ids": sample_counts.most_common(10),
        "vm": command[0],
        "seed": command[1],
        "output_exists": target.is_file(),
    }
    prefix.with_suffix(".json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n")
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0 if result in ("success", "timeout") else 1


if __name__ == "__main__":
    raise SystemExit(main())
