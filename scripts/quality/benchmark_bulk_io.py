#!/usr/bin/env python3
"""Verified V++ vs Python/C++/Node bulk-I/O benchmark (opt-in, cross-platform).

Samples time the operation inside the child process. The runner owns fixture
generation, independent oracles, calibration, medians, p95, MAD and metadata.
Optional NumPy/pandas/Polars cases are explicitly labelled native-library APIs.
"""

import argparse
from datetime import datetime, timezone
import hashlib
from importlib import metadata as package_metadata
import importlib.util
import json
import math
import os
from pathlib import Path
import platform
import random
import shutil
import statistics
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / "benchmark" / "languages"
IO_CASES = ("read_bytes", "write_bytes", "bytes_sum", "read_text", "stream_64k",
            "seek_sample", "csv_split", "jsonl_parse")
FEATURE_CASES = ("list_append", "map_lookup", "function_calls", "string_concat",
                 "alloc_objects", "exceptions", "recursion", "sort_numbers",
                 "linear_search", "set_dedup", "filter_sum")
ALL_CASES = IO_CASES + FEATURE_CASES
RATE_CASES = FEATURE_CASES + ("seek_sample",)
SOURCES_USED = ("bulk_io.vi", "bulk_io.py", "bulk_io.cpp", "bulk_io.js")


def fixtures(target_dir, kib):
    target_dir.mkdir(parents=True, exist_ok=True)
    size = kib * 1024
    # Varied binary payload, including NUL/0xff. Generated once, outside timing.
    data = bytes(((i * 73 + (i // 97) * 19 + 11) & 255) for i in range(size))
    text_line = "Xin chào Việt Nam 🌿 — dữ liệu đo hiệu năng.\n"
    text_data = text_line * max(1, size // len(text_line.encode("utf-8")))
    lines = max(1, size // 34)
    csv_data = "".join(f"{i},Việt Nam,{i % 97}\n" for i in range(lines))
    # JSONL input is smaller on average to bound full parsing on slow V++ VMs.
    json_lines = max(1, size // 64)
    jsonl_data = "".join(json.dumps({"id": i, "n": i % 97, "tên": "Việt 🌿"},
                                        ensure_ascii=False, separators=(",", ":")) + "\n"
                         for i in range(json_lines))
    payloads = {"binary": data, "text": text_data.encode("utf-8"),
                "csv": csv_data.encode("utf-8"), "jsonl": jsonl_data.encode("utf-8")}
    paths = {}
    info = {}
    for name, content in payloads.items():
        path = target_dir / f"{name}-{kib}k.dat"
        path.write_bytes(content)
        paths[name] = path
        info[name] = {"bytes": len(content), "sha256": hashlib.sha256(content).hexdigest()}
    expected = {"read_bytes": len(data), "write_bytes": len(data),
                "bytes_sum": sum(data) % 65521,
                "read_text": len(text_data), "stream_64k": len(data),
                "seek_sample": sum(data[(i * 1009 + 17) % len(data)] for i in range(32)),
                "csv_split": sum(i + (i % 97) for i in range(lines)),
                "jsonl_parse": sum(i + (i % 97) for i in range(json_lines))}
    return paths, info, expected


def feature_expected(case, n):
    if case == "filter_sum":
        k = (n - 1) // 3
        return 3 * k * (k + 1) // 2
    if case == "sort_numbers":
        return n * (n - 1) * (n + 1) // 3
    if case == "linear_search":
        return n - 1
    if case == "set_dedup":
        return 136  # 16 distinct integers 0..15 and their sum 120.
    if case == "list_append":
        return n * (n - 1) // 2
    if case in ("map_lookup", "alloc_objects"):
        return n * (n - 1)
    if case == "function_calls":
        return 3 * n * (n - 1) // 2 + 7 * n
    if case == "string_concat":
        return 3 * n
    if case == "exceptions":
        return (n + 15) // 16
    if case == "recursion":
        return 144 * n
    raise ValueError(case)


def version(exe, *args):
    if not exe:
        return None
    try:
        proc = subprocess.run([str(exe), *args], capture_output=True,
                              text=True, timeout=10, check=False)
        return (proc.stdout + proc.stderr).strip().splitlines()[0][:200]
    except (OSError, subprocess.TimeoutExpired, IndexError):
        return "unavailable"


def python_package_version(name):
    try:
        return package_metadata.version(name)
    except package_metadata.PackageNotFoundError:
        return None


def runtime_commands(cli, output):
    result = {"python": ([sys.executable, "-B", str(SOURCES / "bulk_io.py")],
                         {"read_bytes": "read_bytes", "write_bytes": "write_bytes",
                          "bytes_sum": "bytes_sum",
                          "read_text": "read_text", "stream_64k": "stream_64k",
                          "seek_sample": "seek_sample",
                          "csv_split": "csv_split", "jsonl_parse": "jsonl_parse",
                          **{name: name for name in FEATURE_CASES}})}
    if cli and cli.is_file():
        result["vpp-vm"] = ([str(cli), "run", "--compiler=cpp", str(SOURCES / "bulk_io.vi")],
                            {name: name for name in ALL_CASES})
    node = shutil.which("node")
    if node:
        result["node"] = ([node, str(SOURCES / "bulk_io.js")],
                          {name: name for name in ALL_CASES})
    cxx = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++") or shutil.which("cl")
    if cxx:
        binary = output / ("bulk-cpp.exe" if os.name == "nt" else "bulk-cpp")
        if Path(cxx).name.lower() in ("cl", "cl.exe"):
            command = [cxx, "/nologo", "/O2", "/EHsc", "/std:c++17", "/utf-8",
                       str(SOURCES / "bulk_io.cpp"), f"/Fe{binary}"]
        else:
            command = [cxx, "-O3", "-std=c++17", str(SOURCES / "bulk_io.cpp"), "-o", str(binary)]
        subprocess.run(command, cwd=ROOT, check=True, timeout=120)
        result["cpp"] = ([str(binary)], {name: name for name in ALL_CASES if name != "jsonl_parse"})
    if importlib.util.find_spec("numpy"):
        result["python-numpy"] = (result["python"][0],
                                  {"bytes_sum": "numpy_sum", "filter_sum": "numpy_filter_sum"})
    if importlib.util.find_spec("pandas"):
        result["python-pandas"] = (result["python"][0], {"csv_split": "pandas_csv"})
    if importlib.util.find_spec("polars"):
        result["python-polars"] = (result["python"][0], {"csv_split": "polars_csv"})
    result["python-readinto"] = (result["python"][0], {"read_bytes": "readinto_64k"})
    # mmap maps the input and page-touches it, without materializing a bytes array.
    result["python-mmap"] = (result["python"][0], {"read_bytes": "mmap_read"})
    result["python-csv"] = (result["python"][0], {"csv_split": "csv_module"})
    return result, cxx


def launch(command, case, inner_case, path, expected, iterations, timeout, feature_n,
           output_path=None, expected_sha256=None):
    env = os.environ.copy()
    for name in ("VPP_ENABLE_JIT", "VPP_ENABLE_MACHINE_JIT", "VIETVM_ENABLE_JIT"):
        env.pop(name, None)
    env.update(VPP_HOME=str(ROOT), PYTHONDONTWRITEBYTECODE="1", BENCH_CASE=inner_case,
               BENCH_FILE=str(path), BENCH_ITERATIONS=str(iterations),
               BENCH_FILE_BYTES=str(path.stat().st_size),
               BENCH_EXPECT_UNIT=str(expected), BENCH_FEATURE_N=str(feature_n),
               BENCH_OUTPUT=str(output_path) if output_path else "")
    start = time.perf_counter()
    proc = subprocess.run(command, env=env, cwd=ROOT, capture_output=True,
                          text=True, timeout=timeout, check=False)
    wall_ms = (time.perf_counter() - start) * 1000
    if proc.returncode:
        raise RuntimeError(f"{case}/{inner_case} exit={proc.returncode}: "
                           f"{proc.stderr[-1500:]} {proc.stdout[-1000:]}")
    try:
        payload = json.loads(proc.stdout.strip())
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"{case}/{inner_case}: invalid JSON: {proc.stdout[-1200:]}") from exc
    if (payload.get("case") != inner_case or payload.get("unit") != expected
            or payload.get("iterations") != iterations
            or payload.get("checksum") != expected * iterations
            or not isinstance(payload.get("elapsed_ms"), (int, float))
            or not math.isfinite(payload["elapsed_ms"]) or payload["elapsed_ms"] <= 0):
        raise ValueError(f"{case}/{inner_case}: incorrect oracle/timing: {payload}")
    if output_path:
        if (not output_path.is_file() or
                hashlib.sha256(output_path.read_bytes()).hexdigest() != expected_sha256):
            raise ValueError(f"{case}/{inner_case}: output file hash mismatch")
        output_path.unlink()
    return payload["elapsed_ms"], wall_ms


def summarize(measurements, input_bytes):
    # Measurements are already divided by each sample's calibrated iteration count.
    speeds = [input_bytes / (ms * 1000) for ms in measurements]  # decimal MB/s
    med = statistics.median(measurements)
    ordered = sorted(measurements)
    mad = statistics.median(abs(t - med) for t in measurements)
    # High latency means *low* throughput. Pair p95 latency with its corresponding rate.
    p95 = ordered[math.ceil(len(ordered) * 0.95) - 1]
    return {"median_ms": med, "p95_ms": p95,
            "mad_ms": mad, "median_mbps": statistics.median(speeds),
            "mbps_at_p95_latency": input_bytes / (p95 * 1000),
            "samples": len(measurements)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vpp-cli", type=Path, default=ROOT / "build/bin/vpp-cli")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build-benchmark-bulk")
    parser.add_argument("--sizes-kib", default="16,256", help="Comma-separated sizes, e.g. 16,1024,10240")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--target-ms", type=float, default=20)
    parser.add_argument("--timeout-s", type=int, default=120)
    parser.add_argument("--max-vpp-structured-kib", type=int, default=16,
                        help="Skip very slow V++ CSV/JSONL sizes above this threshold (0 disables guard)")
    parser.add_argument("--runtimes", default="", help="Comma-separated runtime names; empty=all available")
    parser.add_argument("--cases", default="", help="Comma-separated operations; empty=all")
    parser.add_argument("--smoke", action="store_true", help="Quick validation, 4 KiB, one measurement")
    args = parser.parse_args()
    if args.smoke:
        args.sizes_kib, args.runs = "4", 1
    sizes = [int(s) for s in args.sizes_kib.split(",")]
    if (not sizes or len(set(sizes)) != len(sizes) or any(x < 1 or x > 102400 for x in sizes)
            or args.runs < 1 or args.runs > 30 or args.target_ms < 1
            or args.timeout_s < 1 or args.max_vpp_structured_kib < 0):
        parser.error("invalid sizes/runs/target/timeout")
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cli = args.vpp_cli.resolve()
    runtimes, cxx = runtime_commands(cli, out)
    chosen = args.runtimes.split(",") if args.runtimes else list(runtimes)
    if len(set(chosen)) != len(chosen) or any(name not in runtimes for name in chosen):
        parser.error("unsupported or missing runtime: " + args.runtimes)
    selected = {name: runtimes[name] for name in chosen}
    cases = args.cases.split(",") if args.cases else list(ALL_CASES)
    if len(set(cases)) != len(cases) or any(case not in ALL_CASES for case in cases):
        parser.error("unsupported or duplicate operation: " + args.cases)
    revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                              capture_output=True, text=True, check=False)
    git_state = subprocess.run(["git", "status", "--porcelain", "--untracked-files=all"],
                               cwd=ROOT, capture_output=True, text=True, check=False)
    metadata = {"schema": 3, "created_utc": datetime.now(timezone.utc).isoformat(),
                "revision": revision.stdout.strip() if revision.returncode == 0 else None,
                "working_tree_dirty": bool(git_state.stdout.strip()) if git_state.returncode == 0 else None,
                "host": {"system": platform.system(), "release": platform.release(),
                         "machine": platform.machine(), "processor": platform.processor()},
                "versions": {"vpp": version(cli, "--version") if cli.is_file() else None,
                             "python": sys.version.split()[0], "cpp": version(cxx, "--version"),
                             "node": version(shutil.which("node"), "--version"),
                             "numpy": python_package_version("numpy"),
                             "pandas": python_package_version("pandas"),
                             "polars": python_package_version("polars")},
                "source_sha256": {name: hashlib.sha256((SOURCES / name).read_bytes()).hexdigest()
                                  for name in SOURCES_USED} | {
                                      "benchmark_bulk_io.py": hashlib.sha256(
                                          Path(__file__).read_bytes()).hexdigest()},
                "sizes_kib": sizes, "runs": args.runs, "runtimes": chosen, "cases": cases,
                "timing": "child in-process timed operation; excludes startup/compilation/preflight",
                "cache": "warm filesystem cache likely; no OS cache eviction",
                "throughput": "MB/s for full-file operations; seek_sample and language features use batches/s",
                "latency_percentile": "p95_ms is nearest-rank p95; mbps_at_p95_latency is reciprocal throughput for that latency",
                "feature_rate": "whole workload batches/second; each batch contains feature_n elements",
                "semantics": {"read_bytes": "materialize byte array/list in memory",
                              "write_bytes": "write preloaded binary byte buffer and close",
                              "bytes_sum": "read then sum every byte, modulo 65521",
                              "read_text": "decode UTF-8 and count Unicode codepoints",
                              "stream_64k": "read in chunks and count bytes",
                              "seek_sample": "32 pseudo-random one-byte positional reads; report batches/s, not full-file MB/s",
                              "csv_split": "three-column unquoted CSV, split and sum two numeric fields",
                              "jsonl_parse": "JSON decode each line, sum id and n",
                              "list_append": "append N integers then sum",
                              "map_lookup": "insert N string keys then look up all",
                              "function_calls": "call a small arithmetic function N times",
                              "string_concat": "append V++ N times, count characters",
                              "alloc_objects": "allocate N two-field objects then sum values",
                              "exceptions": "throw/catch on every sixteenth iteration",
                              "recursion": "compute recursive Fibonacci(12) N times",
                              "sort_numbers": "sort descending integers via each language's collection API",
                              "linear_search": "search for last element in ascending integer list",
                              "set_dedup": "unique values 0..15 and aggregate sum",
                              "filter_sum": "construct N-element list and sum elements divisible by three"},
                "method_notes": {"python-mmap": "mmap page-touch, avoids materializing bytes; not a like-for-like allocation comparison",
                                 "python-readinto": "buffer reused for 64 KiB streaming reads",
                                 "python-numpy": "vectorized NumPy byte reduction / integer filtering",
                                 "python-pandas": "pandas DataFrame CSV parser and vectorized sum",
                                 "python-polars": "Polars DataFrame CSV parser and vectorized sum",
                                 "python-csv": "stdlib csv.reader, full CSV semantics",
                                 "vpp-vm": "V++ file byte buffers are VM lists, with different representation costs"}}
    records, summary, fixtures_meta, errors, skipped = [], [], {}, [], []
    randomizer = random.Random(20261110)
    for size_kib in sizes:
        paths, info, expected = fixtures(out / "fixtures", size_kib)
        fixtures_meta[str(size_kib)] = info
        feature_n = min(1024, max(32, size_kib * 4))
        items = [(name, method, case) for name, (command, methods) in selected.items()
                 for case, method in methods.items() if case in cases]
        randomizer.shuffle(items)
        for runtime, method, case in items:
            if (runtime == "vpp-vm" and case in ("csv_split", "jsonl_parse")
                    and args.max_vpp_structured_kib > 0
                    and size_kib > args.max_vpp_structured_kib):
                skipped.append({"kib": size_kib, "runtime": runtime, "case": case,
                                "reason": "slow VM structured-data workload size guard"})
                print(f"SKIP {size_kib} KiB {case}/{runtime}: size guard", flush=True)
                continue
            source = {"read_bytes": "binary", "write_bytes": "binary",
                      "bytes_sum": "binary", "read_text": "text",
                      "stream_64k": "binary", "seek_sample": "binary",
                      "csv_split": "csv", "jsonl_parse": "jsonl"}.get(case, "binary")
            path = paths[source]
            case_n = min(feature_n, 64) if case == "sort_numbers" else (
                min(feature_n, 128) if case == "set_dedup" else feature_n)
            unit = (expected[case] if case in IO_CASES else feature_expected(case, case_n))
            byte_size = info[source]["bytes"] if case in IO_CASES else 0
            command = selected[runtime][0]
            label = f"{size_kib} KiB {case}/{runtime}"
            output_path = out / "writes" / f"{size_kib}k-{runtime}-{method}.bin" if case == "write_bytes" else None
            if output_path:
                output_path.parent.mkdir(exist_ok=True)
            expected_sha = info["binary"]["sha256"] if output_path else None
            try:
                pilot, wall = launch(command, case, method, path, unit, 1, args.timeout_s,
                                     case_n, output_path, expected_sha)
                iterations = min(1000000, max(1, math.ceil(args.target_ms / pilot)))
                # Do not amplify expensive V++ cases just to satisfy calibration.
                measurements, walls = [], []
                for round_index in range(args.runs):
                    ms, process_ms = launch(command, case, method, path, unit,
                                            iterations, args.timeout_s, case_n,
                                            output_path, expected_sha)
                    measurements.append(ms / iterations)
                    walls.append(process_ms)
                    records.append({"kib": size_kib, "runtime": runtime, "case": case,
                                    "method": method, "sample": round_index,
                                    "input_bytes": byte_size, "iterations": iterations,
                                    "feature_n": case_n if case in FEATURE_CASES else None,
                                    "elapsed_ms": ms, "ms_per_operation": ms / iterations,
                                    "wall_ms": process_ms, "unit": unit})
                stats = summarize(measurements, byte_size)
                stats.update({"kib": size_kib, "runtime": runtime, "case": case,
                              "method": method, "input_bytes": byte_size,
                              "feature_n": case_n if case in FEATURE_CASES else None,
                              "pilot_ms": pilot, "pilot_wall_ms": wall,
                              "median_process_wall_ms": statistics.median(walls),
                              "iterations_per_sample": iterations})
                if case in RATE_CASES:
                    stats["median_mbps"] = None
                    stats["mbps_at_p95_latency"] = None
                    stats["median_operations_per_s"] = 1000 / stats["median_ms"]
                summary.append(stats)
                throughput = (f"{stats['median_operations_per_s']:.2f} batches/s" if case in RATE_CASES
                              else f"{stats['median_mbps']:.4f} MB/s")
                print(f"PASS {label}: {stats['median_ms']:.6f} ms/op, {throughput}", flush=True)
            except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as exc:
                errors.append({"kib": size_kib, "runtime": runtime, "case": case,
                               "method": method, "error": str(exc)})
                print(f"FAIL {label}: {exc}", file=sys.stderr, flush=True)
    report = {**metadata, "fixtures": fixtures_meta, "records": records,
              "summary": summary, "errors": errors, "skipped": skipped,
              "unsupported": {"cpp": ["jsonl_parse"],
                              "optional": ["python-numpy", "python-pandas", "python-polars"]}}
    (out / "results.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    lines = ["| Size KiB / feature N | Workload | Runtime / method | Median ms/batch | MB/s or batches/s | MAD ms | p95 ms |",
             "| ---: | --- | --- | ---: | ---: | ---: | ---: |"]
    for s in sorted(summary, key=lambda row: (row["kib"], row["case"], row["median_ms"])):
        rate = (f"{s['median_operations_per_s']:.2f} batches/s" if s['case'] in RATE_CASES
                else f"{s['median_mbps']:.4f} MB/s")
        size_text = (f"N={s['feature_n']}" if s['case'] in FEATURE_CASES else str(s['kib']))
        lines.append(f"| {size_text} | {s['case']} | {s['runtime']} / {s['method']} | "
                     f"{s['median_ms']:.6f} | {rate} | "
                     f"{s['mad_ms']:.6f} | {s['p95_ms']:.6f} |")
    (out / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    # Ratios use the median of matched workload outputs; API/algorithm differences
    # are annotated in BULK_BENCHMARK.md and must not be read as pure VM overhead.
    lookup = {(row["kib"], row["case"], row["runtime"]): row for row in summary}
    ratios = ["# V++ VM / Python median ratios (same host)", "",
              "These ratios reflect the public API workloads, including different allocation and algorithm",
              "costs. They are **not** pure VM dispatch overhead. See `BULK_BENCHMARK.md`.", "",
              "| Size KiB / feature N | Workload | V++ VM median ms | Python median ms | V++ / Python |",
              "| ---: | --- | ---: | ---: | ---: |"]
    for row in sorted((r for r in summary if r["runtime"] == "vpp-vm"),
                      key=lambda r: (r["kib"], r["case"])):
        peer = lookup.get((row["kib"], row["case"], "python"))
        if peer:
            group = f"N={row['feature_n']}" if row["case"] in FEATURE_CASES else str(row["kib"])
            ratios.append(f"| {group} | {row['case']} | {row['median_ms']:.6f} | "
                          f"{peer['median_ms']:.6f} | {row['median_ms'] / peer['median_ms']:.2f}x |")
    (out / "comparisons.md").write_text("\n".join(ratios) + "\n", encoding="utf-8")
    print(f"Results: {out / 'results.json'} ({len(records)} samples, "
          f"{len(skipped)} skipped cells, {len(errors)} failed cells)", flush=True)
    return bool(errors)


if __name__ == "__main__":
    sys.exit(main())
