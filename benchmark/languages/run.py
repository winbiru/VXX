#!/usr/bin/env python3
"""Compare current V++ VM/full AOT with installed C++, Python, Node and Java."""

import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import shutil
import signal
import statistics
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
CASES = ("arithmetic", "branch", "fibonacci", "list", "text", "json")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2, allow_nan=False) + "\n")


def reference(name):
    # Independent known answers, not another timing backend's measured output.
    fib = [6765, 10946, 17711, 28657, 46368, 75025, 121393, 196418,
           317811, 514229, 832040, 1346269, 2178309, 3524578, 5702887, 9227465]
    return [{"arithmetic": lambda: 1020 + 8*i - (i % 7)*(i % 11),
             "branch": lambda: (i*4093 + 7 + 251*(128+i)) % 65521,
             "fibonacci": lambda: fib[i],
             "list": lambda: sum(v for v in ((j*17+i) % 1000 for j in range(128)) if v >= 500),
             "text": lambda: 18*(i+1),
             "json": lambda: 120 + i + 8}[name]() for i in range(16)]


def verify_output(stdout, name, iterations):
    lines = stdout.splitlines()
    if len(lines) != 19:
        raise ValueError(f"expected 16 validation values + timing/checksum/count, got {stdout[:300]!r}")
    actual = [float(value) for value in lines[:16]]
    expected = reference(name)
    if actual != expected:
        raise ValueError(f"{name} output mismatch: {actual} != {expected}")
    elapsed_ms, checksum, reported_iterations = map(float, lines[16:])
    expected_sum = sum(expected) * (iterations // 16) + sum(expected[:iterations % 16])
    if checksum != expected_sum or reported_iterations != iterations:
        raise ValueError(f"{name} timed checksum/iteration mismatch")
    if not math.isfinite(elapsed_ms) or elapsed_ms <= 0:
        raise ValueError("nonpositive/nonfinite measured duration")
    return {"iterations": iterations, "checksum": int(checksum), "validation": expected,
            "elapsed_ms": elapsed_ms, "ns_per_operation": elapsed_ms * 1e6 / iterations}


def capture(args, cwd=ROOT):
    return subprocess.check_output(args, cwd=cwd, stderr=subprocess.STDOUT, text=True, timeout=60).strip()


def measure(argv, env, name, iterations):
    # posix_spawn avoids inheriting a large Python heap before exec; wait4 gives
    # this child's own peak RSS, not RUSAGE_CHILDREN's cumulative maximum.
    child_env = dict(env, VPP_BENCH_CASE=name, VPP_BENCH_ITERS=str(iterations))
    started = time.perf_counter()
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        actions = [(os.POSIX_SPAWN_DUP2, out.fileno(), 1), (os.POSIX_SPAWN_DUP2, err.fileno(), 2)]
        pid = os.posix_spawnp(argv[0], argv, child_env, file_actions=actions)
        timed_out = []

        def expire():
            try:
                os.kill(pid, signal.SIGKILL)
                timed_out.append(True)
            except ProcessLookupError:
                pass

        timer = threading.Timer(180, expire)
        timer.daemon = True
        timer.start()
        try:
            _, status, usage = os.wait4(pid, 0)
        finally:
            timer.cancel()
            timer.join()
        wall_ms = (time.perf_counter() - started) * 1000
        out.seek(0)
        err.seek(0)
        stdout, stderr = out.read().decode(), err.read().decode()
    if timed_out or os.waitstatus_to_exitcode(status):
        raise RuntimeError(f"{name}: {argv[0]} failed status {status}: {stderr[:1000]} {stdout[:300]}")
    result = verify_output(stdout, name, iterations)
    result.update(wall_ms=wall_ms, peak_rss_bytes=usage.ru_maxrss * (1 if sys.platform == "darwin" else 1024),
                  stderr=stderr)
    return result


def describe(values):
    ordered = sorted(values)
    median = statistics.median(values)
    mad = statistics.median(abs(value-median) for value in values)
    position = (len(values)-1)*0.95
    lo, hi = math.floor(position), math.ceil(position)
    return {"median": median, "p95": ordered[lo]+(ordered[hi]-ordered[lo])*(position-lo),
            "mad_over_median": mad/median, "min": min(values), "max": max(values), "samples": len(values)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vpp", type=Path, default=ROOT/"build-performance/bin/vpp-cli")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=10)
    parser.add_argument("--target-ms", type=float, default=100)
    args = parser.parse_args()
    if args.samples < 10 or not math.isfinite(args.target_ms) or args.target_ms < 50:
        parser.error("samples >=10 and target-ms >=50 required")
    if not hasattr(os, "posix_spawnp") or not hasattr(os, "wait4"):
        parser.error("this runner requires POSIX posix_spawnp/wait4")
    output, vpp = args.output.resolve(), args.vpp.resolve()
    output.mkdir(parents=True, exist_ok=False)
    if (output/"report.json").exists():
        parser.error("choose a new output directory; existing evidence is never overwritten")
    build_dir = output/"build"
    build_dir.mkdir()
    env = {key: value for key, value in os.environ.items() if not key.startswith("VPP_")}
    env.update(VPP_HOME=str(ROOT), VPP_AOT_ROOT=str(ROOT), VPP_ENABLE_JIT="0", VPP_ENABLE_MACHINE_JIT="0",
               VPP_GC_INTERVAL="256", TZ="UTC")
    languages = {"vpp_vm": [str(vpp), "chạy", str(HERE/"workloads.vi")],
                 "vpp_aot": [str(build_dir/"vpp-workloads")]}
    versions = {"vpp_vm": capture([str(vpp), "phiên bản"]),
                "vpp_aot": "full Native AOT via current C++ Stage 0 --emit-native"}
    missing = [name for name in ("go", "rustc") if not shutil.which(name)]
    commands = []
    cpp, python, node = shutil.which("clang++"), shutil.which("python3"), shutil.which("node")
    java, javac = shutil.which("java"), shutil.which("javac")
    if cpp:
        languages["cpp_O3"] = [str(build_dir/"cpp-workloads")]
        versions["cpp_O3"] = capture([cpp, "--version"]).splitlines()[0]
        commands.append([cpp, "-std=c++17", "-O3", "-DNDEBUG", "-fno-lto", str(HERE/"workloads.cpp"),
                         "-o", str(build_dir/"cpp-workloads")])
    if python:
        languages["python"] = [python, str(HERE/"workloads.py")]
        versions["python"] = capture([python, "--version"])
    if node:
        languages["node"] = [node, str(HERE/"workloads.js")]
        versions["node"] = capture([node, "--version"])
    if java and javac:
        languages["java"] = [java, "-cp", str(build_dir), "Workloads"]
        versions["java"] = capture([java, "-version"]).splitlines()[0]
        commands.append([javac, "-encoding", "UTF-8", "-d", str(build_dir), str(HERE/"Workloads.java")])
    commands.append([str(vpp), "--emit-native", str(HERE/"workloads.vi"), str(build_dir/"vpp-workloads")])
    with (output/"build.log").open("w") as log:
        for command in commands:
            print("Building:", command[0], flush=True)
            log.write(json.dumps(command)+"\n")
            log.flush()
            subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                           timeout=600, check=True)
    cpu = platform.processor()
    if sys.platform == "darwin":
        hardware = json.loads(capture(["/usr/sbin/system_profiler", "SPHardwareDataType", "-json"]))
        cpu = hardware["SPHardwareDataType"][0].get("chip_type", cpu)
    copied = output/"sources"
    copied.mkdir()
    for path in HERE.iterdir():
        if path.is_file():
            shutil.copy2(path, copied/path.name)
    provenance = {path.name: sha(path) for path in copied.iterdir()}
    binaries = {name: {"command": command, "executable_sha256": sha(Path(command[0]))}
                for name, command in languages.items()}
    report = {"schema": 1, "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "commit": capture(["git", "rev-parse", "HEAD"]), "dirty_status": capture(["git", "status", "--short"]),
              "system": {"os": platform.platform(), "cpu": cpu, "architecture": platform.machine()},
              "versions": versions, "unavailable_tools": missing, "commands": commands,
              "source_sha256": provenance, "vpp_cli_sha256": sha(vpp), "binaries": binaries,
              "protocol": {"samples_per_case_backend": args.samples, "warmup_batches_per_process": 3,
                           "target_ms": args.target_ms, "input_count": 16, "shuffle_seed": 20261024,
                           "jit": "V++ VM disabled; Node/Java default JIT after warmups",
                           "rss": "wait4 per-child process lifetime peak; bytes",
                           "timer": "in-process final batch; excludes startup/compilation/input setup/warmups"},
              "environment": {key: value for key, value in env.items() if key.startswith("VPP_") or key == "TZ"},
              "skipped": [{"backend": name, "case": "json", "reason": "no JSON library dependency added"}
                          for name in ("cpp_O3", "java") if name in languages],
              "plan": [], "pilots": [], "samples": [], "summary": {}}
    write_json(output/"manifest.json", report)
    # Calibrate each runtime to useful duration, always multiples of the same
    # complete 16-element input cycle. The final plan is frozen before samples.
    for name in CASES:
        for backend, command in languages.items():
            if name == "json" and backend in ("cpp_O3", "java"):
                continue
            iterations = 16
            for pilot in range(5):
                result = measure(command, env, name, iterations)
                report["pilots"].append({"case": name, "backend": backend, "record": result})
                if result["elapsed_ms"] >= args.target_ms*0.8 or iterations >= 100000000:
                    break
                if pilot < 4:
                    estimate = min(100000000, max(iterations+16,
                        math.ceil(iterations*args.target_ms/max(result["elapsed_ms"], 0.001)/16)*16))
                    iterations = estimate
            report["plan"].append({"case": name, "backend": backend, "iterations": iterations})
            print(f"Pilot {name}/{backend}: {iterations} operations, {result['elapsed_ms']:.1f} ms", flush=True)
    write_json(output/"manifest.json", {k:v for k,v in report.items() if k != "samples"})
    rng = random.Random(20261024)
    with (output/"samples.jsonl").open("w") as raw:
        for name in CASES:
            plan = [row for row in report["plan"] if row["case"] == name]
            print(f"Measuring {name}: {len(plan)} backends x {args.samples} processes", flush=True)
            for sample in range(args.samples):
                order = list(plan)
                rng.shuffle(order)
                for position, item in enumerate(order):
                    backend = item["backend"]
                    result = measure(languages[backend], env, name, item["iterations"])
                    row = {"case": name, "backend": backend, "sample": sample, "order": position, "record": result}
                    raw.write(json.dumps(row, ensure_ascii=False)+"\n")
                    raw.flush()
                    report["samples"].append(row)
            report["summary"][name] = {}
            for item in plan:
                rows = [row["record"] for row in report["samples"] if row["case"] == name
                        and row["backend"] == item["backend"]]
                report["summary"][name][item["backend"]] = {
                    metric: describe([row[metric] for row in rows])
                    for metric in ("ns_per_operation", "wall_ms", "peak_rss_bytes")}
    report["correctness"] = "PASS (all 16 reference outputs and every warm/timed checksum)"
    write_json(output/"report.json", report)
    names = list(languages)
    lines = ["Median microseconds/operation; lower is faster.",
             "case | " + " | ".join(names)]
    for name, summary in report["summary"].items():
        cells = []
        for backend in names:
            if backend not in summary:
                cells.append("N/A")
            else:
                stats = summary[backend]["ns_per_operation"]
                cells.append(f"{stats['median']/1000:.4f}" + (" *" if stats['mad_over_median'] > .05 else ""))
        lines.append(name + " | " + " | ".join(cells))
    lines.append("* MAD/median >5%; noisy, no precise ranking claimed.")
    lines.append(report["correctness"])
    summary = "\n".join(lines)+"\n"
    (output/"summary.txt").write_text(summary)
    print(summary, flush=True)


if __name__ == "__main__":
    main()
