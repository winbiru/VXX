#!/usr/bin/env python3
"""Đo AOT V++ hiện tại bên cạnh C, Rust, C++, Node, Java, Python và Ruby.

Ứng dụng V++ dùng bộ đo viết bằng V++; C/Rust và i32 dùng cùng driver C++.
Không suy diễn thời gian một lời gọi máy từ phép đo ứng dụng động.
"""

import argparse
import hashlib
import json
import os
import platform
import random
import statistics
import subprocess
from datetime import datetime, timezone
from pathlib import Path


CASES = ("arithmetic", "branch_loop", "fibonacci", "list_128", "unicode_replace", "json_parse")
NUMERIC = CASES[:3]
AOT_NAME = "vpp-aot-vpp"
MAX_ITERATIONS = 67_108_864  # Chia hết cho 16, nằm dưới giới hạn của driver.


def expected(case, k):
    if case == "arithmetic":
        return 27 * k + 475
    if case == "branch_loop":
        return (k * 97 + 11 + 256 * 251) % 65521
    if case == "fibonacci":
        values = (6765, 10946, 17711, 28657, 46368, 75025, 121393, 196418,
                  317811, 514229, 832040, 1346269, 2178309, 3524578, 5702887, 9227465)
        return values[k % 16]
    if case == "list_128":
        return 64 * k + 4032 + (k % 2) * 64
    if case == "unicode_replace":
        return (k % 4 + 4) * 26 + len(str(k))
    if case == "json_parse":
        return 17 * k + 130
    raise ValueError(case)


def checksum(case, iterations, seed=17):
    return sum(expected(case, seed + i) % 997 for i in range(16)) * (iterations // 16)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=Path("benchmark/reports/languages-2026-11-03-aot-vpp/manifest.json"))
    parser.add_argument("--aot", type=Path, default=Path("build-benchmark-2026-11-04/application-vpp"))
    parser.add_argument("--typed", type=Path, default=Path("build-benchmark-2026-11-04/typed"))
    parser.add_argument("--c", type=Path, default=Path("build-benchmark-2026-11-04/c"))
    parser.add_argument("--rust", type=Path, default=Path("build-benchmark-2026-11-04/rust"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=9)
    parser.add_argument("--target-ms", type=float, default=60)
    parser.add_argument("--runtimes", default="vpp-aot-vpp,vpp-i32,c,rust,cpp,node,java,python,ruby",
                        help="Comma-separated subset of benchmark runtimes available on this OS")
    parser.add_argument("--environment-label", default="native host",
                        help="Measurement environment (for example Docker ARM64 or emulated x86_64)")
    args = parser.parse_args()
    if args.runs < 3 or args.target_ms < 10:
        parser.error("Cần ít nhất 3 mẫu và target >= 10 ms")
    requested = args.runtimes.split(",")
    valid = {AOT_NAME, "vpp-aot", "vpp-vm", "vpp-i32", "c", "rust", "cpp", "node", "java", "python", "ruby"}
    if not requested or len(set(requested)) != len(requested) or any(name not in valid for name in requested):
        parser.error("Danh sách --runtimes không hợp lệ hoặc bị trùng")
    manifest = json.loads(args.manifest.read_text())
    selected = {r["name"]: r for r in manifest["runtimes"] if r["name"] in requested}
    for name, path, cases, option in ((AOT_NAME, args.aot, CASES, "aot"),
                                     ("vpp-i32", args.typed, NUMERIC, "typed"),
                                     ("c", args.c, NUMERIC, "c"),
                                     ("rust", args.rust, NUMERIC, "rust")):
        # Prefer the manifest's freshly built executable. Only override it
        # when the runner is missing or a non-default path was requested.
        override = name not in selected or path != parser.get_default(option)
        if name in requested and override:
            selected[name] = dict(name=name, executable=str(path.resolve()), arguments=[],
                                  environment={}, cases=list(cases))
    missing = [name for name in requested if name not in selected]
    if missing:
        raise ValueError(f"Manifest thiếu các bộ chạy: {', '.join(missing)}")
    order_names = requested
    runners = [selected[name] for name in order_names]
    metadata = []
    for runner in runners:
        path = Path(runner["executable"])
        metadata.append(dict(name=runner["name"], executable=str(path),
                             sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                             arguments=runner["arguments"], cases=runner["cases"]))
    # A stock Windows Python does not ship the IANA zone database.
    report = dict(date=datetime.now(timezone.utc).date().isoformat(),
                  generated_at_utc=datetime.now(timezone.utc).isoformat(),
                  platform=platform.platform(), machine=platform.machine(),
                  environment_label=args.environment_label,
                  seed=17, runs=args.runs, target_ms=args.target_ms,
                  warmup_batches=3, measurement="in-process batch, excludes compilation and startup",
                  manifest=str(args.manifest.resolve()),
                  manifest_sha256=hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
                  runtime_metadata=metadata, samples=[], calibration={}, summary={})

    def run(runner, case, n):
        env = os.environ.copy()
        for key in ("VPP_VM_PROFILE", "VPP_AOT_CPU_PROFILE", "VPP_ENABLE_JIT",
                    "VPP_ENABLE_MACHINE_JIT", "VIETVM_ENABLE_JIT"):
            env.pop(key, None)
        env.update(VPP_HOME=manifest["root"], BENCH_CASE=case,
                   BENCH_ITERATIONS=str(n), BENCH_SEED="17", PYTHONDONTWRITEBYTECODE="1")
        env.update(runner.get("environment", {}))
        p = subprocess.run([runner["executable"], *runner["arguments"]],
                           env=env, capture_output=True, text=True, timeout=120)
        if p.returncode:
            raise RuntimeError(f"Benchmark process failed ({runner['name']}/{case}, n={n}, "
                               f"exit={p.returncode}):\nstdout: {p.stdout[-4000:]}\n"
                               f"stderr: {p.stderr[-4000:]}")
        row = json.loads(p.stdout.strip())
        checks = [expected(case, 17 + i) for i in range(16)]
        if (row["case"] != case or row["iterations"] != n or row["seed"] != 17
                or row["checks"] != checks
                or int(row["checksum"]) != checksum(case, n)
                or row["elapsed_ms"] <= 0 or len(row["warmup_ms"]) != 3
                or not all(value > 0 for value in row["warmup_ms"])):
            raise ValueError(f"Kiểm tra kết quả thất bại: {runner['name']} / {case}")
        elapsed_ns = row["elapsed_ms"] * 1_000_000 / n
        if abs(row["ns_per_op"] - elapsed_ns) > max(0.01, elapsed_ns * 1e-6):
            raise ValueError(f"Sai chuẩn hóa thời gian: {runner['name']} / {case}")
        return row

    tasks = [(runner, case) for case in CASES for runner in runners if case in runner["cases"]]
    counts = {}
    for runner, case in tasks:
        key = f"{runner['name']}/{case}"
        n = 1024 if case in NUMERIC and runner["name"] not in (AOT_NAME, "vpp-aot") else 16
        for attempt in range(7):
            result = run(runner, case, n)
            if result["elapsed_ms"] >= args.target_ms * 0.7 or n >= MAX_ITERATIONS:
                break
            estimate = (n * args.target_ms / result["elapsed_ms"])
            candidate = min(MAX_ITERATIONS, max(n * 2, int(estimate / 16) * 16))
            if candidate == n:
                break
            n = candidate
        counts[key] = n
        report["calibration"][key] = dict(iterations=n, last_pilot_ms=result["elapsed_ms"])
        print(f"Hiệu chỉnh {key}: {n:,} lượt", flush=True)

    rng = random.Random(110426)
    for index in range(args.runs):
        shuffled = tasks[:]
        rng.shuffle(shuffled)
        for runner, case in shuffled:
            name = runner["name"]
            row = run(runner, case, counts[f"{name}/{case}"])
            report["samples"].append(dict(runtime=name, case=case, run=index,
                                          iterations=row["iterations"], checks=row["checks"],
                                          checksum=row["checksum"], elapsed_ms=row["elapsed_ms"],
                                          ns_per_op=row["ns_per_op"], warmup_ms=row["warmup_ms"]))
        print(f"Xong lượt {index + 1}/{args.runs}", flush=True)

    for runner, case in tasks:
        name = runner["name"]
        values = [r["ns_per_op"] for r in report["samples"]
                  if r["runtime"] == name and r["case"] == case]
        median = statistics.median(values)
        mad = statistics.median(abs(x - median) for x in values)
        report["summary"][f"{name}/{case}"] = dict(median_ns=median, mad_ns=mad,
                                                        mad_fraction=mad / median, samples=len(values))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(f"ĐÃ XÁC MINH {len(report['samples'])} mẫu → {args.output}", flush=True)
    for case in CASES:
        print(case, " ".join(f"{name}={report['summary'][f'{name}/{case}']['median_ns']/1000:.3f} µs"
                             for name in order_names if f"{name}/{case}" in report["summary"]), flush=True)


if __name__ == "__main__":
    main()
