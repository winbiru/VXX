#!/usr/bin/env python3
"""Compare completed ASM-VM compiler builds on the same source and host.

Example:
    python3 scripts/quality/benchmark_vm_compiler.py \
        --vm build/vm-asm-check/vpp-vm \
        --source scripts/test/kiem_tra_ffi_tep.vi \
        baseline=build/vm-asm-selfhost/compiler-stage1.vmb \
        optimized=build/ci-investigation/compiler-lexer-column.vmb

Unlike time-limited VM profiling, every measurement compiles the *entire*
source. Compare elapsed time only across runs with matching artifact hashes.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs="+", metavar="LABEL=IMAGE")
    parser.add_argument("--vm", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--output-dir", type=Path,
                        default=Path("build/ci-investigation/compiler-benchmark"))
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    if args.repeats < 1 or args.warmups < 0 or args.timeout < 1:
        parser.error("repeats and timeout must be positive; warmups nonnegative")

    root = args.root.resolve()
    vm = args.vm.resolve()
    source = args.source.resolve()
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["VPP_HOME"] = str(root)

    images = []
    for spec in args.images:
        if "=" not in spec:
            parser.error(f"expected LABEL=IMAGE: {spec}")
        label, name = spec.split("=", 1)
        if not label or not label.replace("-", "_").isidentifier():
            parser.error(f"invalid label: {label}")
        images.append((label, Path(name).resolve()))
    if len({label for label, _ in images}) != len(images):
        parser.error("duplicate labels")

    results = []
    for label, image in images:
        elapsed = []
        digests = set()
        for index in range(args.warmups + args.repeats):
            target = output_dir / f"{label}-{index}.vmb"
            target.unlink(missing_ok=True)
            command = [str(vm), str(image), "build", str(source), "-o", str(target)]
            started = time.perf_counter()
            try:
                run = subprocess.run(command, cwd=root, env=env,
                                     capture_output=True, text=True,
                                     timeout=args.timeout, check=False)
            except subprocess.TimeoutExpired as exc:
                raise SystemExit(f"{label} run {index} timed out after {args.timeout}s") from exc
            seconds = time.perf_counter() - started
            if run.returncode != 0 or not target.is_file():
                raise SystemExit(f"{label} run {index} failed ({run.returncode}):\n"
                                 f"{run.stdout[-2000:]}\n{run.stderr[-4000:]}")
            digest = hashlib.sha256(target.read_bytes()).hexdigest()
            digests.add(digest)
            if index >= args.warmups:
                elapsed.append(round(seconds, 4))
            print(f"{label} {'warmup' if index < args.warmups else 'run'} "
                  f"{index}: {seconds:.3f}s sha256={digest[:16]}", flush=True)
        if len(digests) != 1:
            raise SystemExit(f"{label}: output changed between repeated compilations")
        results.append({"label": label, "image": str(image),
                        "times_seconds": elapsed,
                        "median_seconds": statistics.median(elapsed),
                        "sha256": next(iter(digests))})

    if len({item["sha256"] for item in results}) != 1:
        raise SystemExit("Different compiler versions produced different bytecode!\n"
                         + json.dumps(results, ensure_ascii=False, indent=2))
    destination = output_dir / "results.json"
    destination.write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8")
    print(f"All images agree byte-for-byte; results: {destination}")


if __name__ == "__main__":
    main()
