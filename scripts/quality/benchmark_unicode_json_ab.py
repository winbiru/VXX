#!/usr/bin/env python3
"""Compare native V++ Unicode/JSON changes against a clean Git revision.

Both variants use the exact same native compiler and benchmark source. The
baseline is compiled in a temporary, isolated Git worktree, so import lookup
cannot accidentally resolve to the updated ``gói/dữ liệu/json/json.vi``.
The benchmark reports in-process elapsed time, excluding compilation/startup.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path("benchmark/unicode_json_ab.vi")
JSON_SOURCE = Path("gói/dữ liệu/json/json.vi")
CASES = ("json_escape_plain", "json_escape_mixed", "unicode_replace_ascii", "unicode_replace_mixed")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def execute(args, cwd, **kwargs):
    return subprocess.run(list(map(str, args)), cwd=cwd, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-compiler", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--baseline-ref", default="HEAD", help="Git revision for the baseline")
    parser.add_argument("--runs", type=int, default=11)
    parser.add_argument("--iterations", type=int, default=16)
    args = parser.parse_args()
    if args.runs < 3 or args.iterations < 1 or args.iterations > 100000:
        parser.error("Require at least 3 runs and 1..100000 iterations")
    compiler = args.native_compiler.resolve()
    if not compiler.is_file():
        parser.error(f"Compiler is missing: {compiler}")
    source = ROOT / SOURCE
    if not source.is_file():
        parser.error(f"Benchmark source is missing: {source}")
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binaries = {"baseline": output / "unicode-json-baseline", "updated": output / "unicode-json-updated"}

    with tempfile.TemporaryDirectory(prefix="vpp-unicode-json-ab-") as temp:
        baseline = Path(temp) / "baseline"
        execute(["git", "worktree", "add", "--detach", baseline, args.baseline_ref], ROOT,
                stdout=subprocess.DEVNULL)
        try:
            baseline_source = baseline / SOURCE
            baseline_source.parent.mkdir(parents=True, exist_ok=True)
            baseline_source.write_bytes(source.read_bytes())  # Identical benchmark for A/B.
            source_hashes = {
                "baseline_json": sha256(baseline / JSON_SOURCE),
                "updated_json": sha256(ROOT / JSON_SOURCE),
                "benchmark": sha256(source),
            }
            if source_hashes["baseline_json"] == source_hashes["updated_json"]:
                raise RuntimeError("Baseline and updated JSON sources are identical")
            for name, project, program in (("baseline", baseline, baseline_source),
                                           ("updated", ROOT, source)):
                env = os.environ.copy()
                env["VPP_HOME"] = str(project)
                execute([compiler, "build", program, "-o", binaries[name]], project, env=env)
        finally:
            execute(["git", "worktree", "remove", "--force", baseline], ROOT,
                    stdout=subprocess.DEVNULL)

    binary_hashes = {name: sha256(path) for name, path in binaries.items()}
    if binary_hashes["baseline"] == binary_hashes["updated"]:
        raise RuntimeError("A/B binaries are identical; module resolution is not isolated")

    samples = []
    pairs = [(case, variant) for case in CASES for variant in binaries]
    rng = random.Random(20261108)
    for i in range(args.runs):
        rng.shuffle(pairs)
        for case, variant in pairs:
            env = os.environ.copy()
            env.update(BENCH_CASE=case, BENCH_ITERATIONS=str(args.iterations))
            completed = execute([binaries[variant]], ROOT, env=env, text=True,
                                capture_output=True, timeout=120)
            lines = completed.stdout.strip().splitlines()
            if len(lines) != 3:
                raise RuntimeError(f"Invalid benchmark output for {case}/{variant}: {completed.stdout}")
            elapsed, checksum, unit = map(float, lines)
            if elapsed <= 0 or checksum != unit * args.iterations:
                raise RuntimeError(f"Invalid checksum or clock for {case}/{variant}")
            samples.append({"case": case, "variant": variant, "run": i,
                            "ms_per_op": elapsed / args.iterations,
                            "checksum": int(checksum), "unit": int(unit)})
        print(f"A/B sample {i + 1}/{args.runs}", flush=True)

    summary = {}
    for case in CASES:
        variants = {}
        for variant in binaries:
            rows = [x for x in samples if x["case"] == case and x["variant"] == variant]
            values = [x["ms_per_op"] for x in rows]
            median = statistics.median(values)
            variants[variant] = {"median_ms_per_op": median,
                                 "mad_ms_per_op": statistics.median(abs(x - median) for x in values),
                                 "checksum": rows[0]["checksum"], "unit": rows[0]["unit"]}
            if any(r["checksum"] != rows[0]["checksum"] or r["unit"] != rows[0]["unit"] for r in rows):
                raise RuntimeError(f"Inconsistent checksums for {case}/{variant}")
        if (variants["baseline"]["checksum"], variants["baseline"]["unit"]) != (
            variants["updated"]["checksum"], variants["updated"]["unit"]
        ):
            raise RuntimeError(f"A/B output mismatch for {case}")
        variants["speedup"] = variants["baseline"]["median_ms_per_op"] / variants["updated"]["median_ms_per_op"]
        summary[case] = variants
        print(f"{case}: baseline={variants['baseline']['median_ms_per_op']:.3f} ms/op, "
              f"updated={variants['updated']['median_ms_per_op']:.3f} ms/op, "
              f"speedup={variants['speedup']:.2f}x", flush=True)

    report = {"date_utc": datetime.now(timezone.utc).isoformat(),
              "platform": platform.platform(), "machine": platform.machine(),
              "baseline_ref": args.baseline_ref,
              "baseline_commit": execute(["git", "rev-parse", args.baseline_ref], ROOT,
                                         text=True, capture_output=True).stdout.strip(),
              "updated_commit": execute(["git", "rev-parse", "HEAD"], ROOT,
                                        text=True, capture_output=True).stdout.strip(),
              "compiler_sha256": sha256(compiler), "source_sha256": source_hashes,
              "binary_sha256": binary_hashes,
              "iterations": args.iterations, "runs": args.runs,
              "summary": summary, "samples": samples}
    report_file = output / "unicode-json-ab.json"
    report_file.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("Report:", report_file)


if __name__ == "__main__":
    main()
