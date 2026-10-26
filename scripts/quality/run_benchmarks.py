#!/usr/bin/env python3
"""Build two revisions and measure them alternately on this host, then replay the gate."""

import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

from compare_benchmarks import compare, summarize


ROOT = Path(__file__).resolve().parents[2]
HARNESS_FILES = ("CMakeLists.txt", "benchmark_baseline.cpp", "telemetry.cpp",
                 "telemetry.h", "benchmark_config.h.in")
CONFIG = ["-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTS=OFF", "-DBUILD_BENCHMARKS=ON",
          "-DBUILD_SHARED_LIBS=OFF", "-DVPP_ENABLE_COVERAGE=OFF",
          "-DVPP_ENABLE_CLANG_TIDY=OFF"]


def command(args, cwd=ROOT, env=None):
    return subprocess.check_output(args, cwd=cwd, env=env, stderr=subprocess.STDOUT, timeout=300)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2, allow_nan=False) + "\n")


def tree_digest(source):
    hasher = hashlib.sha256()
    for path in sorted(source.rglob("*")):
        if path.is_file():
            hasher.update(str(path.relative_to(source)).encode())
            hasher.update(b"\0" + path.read_bytes() + b"\0")
    return hasher.hexdigest()


def snapshot(ref, destination, output):
    """Extract without modifying Git's index/worktrees or running checkout hooks."""
    revision = "HEAD" if ref == "WORKTREE" else ref
    sha = command(["git", "rev-parse", "--verify", revision + "^{commit}"]).decode().strip()
    archive = destination.parent / (destination.name + ".tar")
    with archive.open("wb") as stream:
        subprocess.run(["git", "archive", "--format=tar", sha], cwd=ROOT,
                       stdout=stream, check=True)
    destination.mkdir()
    subprocess.run(["tar", "-xf", str(archive), "-C", str(destination)], check=True)
    archive.unlink()
    patch = b""
    untracked = []
    if ref == "WORKTREE":
        # Include staged and unstaged changes together, including tracked deletions.
        patch = command(["git", "diff", "--binary", "HEAD", "--", "."])
        files = command(["git", "ls-files", "-z"]).split(b"\0")
        untracked = [os.fsdecode(name) for name in command(
            ["git", "ls-files", "--others", "--exclude-standard", "-z"]).split(b"\0")
                     if name and not (ROOT / os.fsdecode(name)).is_relative_to(output)
                     and not os.fsdecode(name).startswith("benchmark/reports/")]
        for name in files + [os.fsencode(name) for name in untracked]:
            if not name:
                continue
            relative = Path(os.fsdecode(name))
            original, target = ROOT / relative, destination / relative
            if original.is_symlink():
                target.parent.mkdir(parents=True, exist_ok=True)
                target.unlink(missing_ok=True)
                target.symlink_to(os.readlink(original))
            elif original.is_file():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(original, target)
            elif target.exists():
                target.unlink()
    (output / (destination.name + ".patch")).write_bytes(patch)
    return {"ref": ref, "commit": sha, "dirty": bool(patch or untracked),
            "patch_sha256": digest(patch), "untracked_files": untracked,
            "snapshot_sha256": tree_digest(destination)}


def build(source, directory, log, jobs, env):
    with log.open("w") as stream:
        for args in (["cmake", "-S", str(source), "-B", str(directory)] + CONFIG,
                     ["cmake", "--build", str(directory), "--config", "Release", "--parallel",
                      str(jobs), "--target", "vpp-benchmark-baseline"]):
            stream.write(json.dumps(args) + "\n")
            stream.flush()
            subprocess.run(args, cwd=ROOT, env=env, stdout=stream,
                           stderr=subprocess.STDOUT, check=True)
    suffix = ".exe" if os.name == "nt" else ""
    binary = directory / "bin" / ("vpp-benchmark-baseline" + suffix)
    if not binary.exists():
        binary = directory / "bin" / "Release" / binary.name
    cache = {}
    for line in (directory / "CMakeCache.txt").read_text().splitlines():
        if ":" in line and "=" in line and not line.startswith(("//", "#")):
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    keys = ("CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER", "CMAKE_CXX_FLAGS", "CMAKE_CXX_FLAGS_RELEASE",
            "CMAKE_EXE_LINKER_FLAGS", "CMAKE_EXE_LINKER_FLAGS_RELEASE",
            "CMAKE_GENERATOR", "CMAKE_OSX_ARCHITECTURES", "CMAKE_OSX_DEPLOYMENT_TARGET",
            "VPP_ENABLE_COVERAGE", "VPP_ENABLE_LEGACY_LIBFFI", "BUILD_SHARED_LIBS")
    meta = {"binary_sha256": digest(binary.read_bytes()),
            "cmake": {key: cache.get(key, "") for key in keys}}
    if meta["cmake"]["CMAKE_BUILD_TYPE"] != "Release":
        raise ValueError("benchmark build is not Release")
    effective_flags = " ".join(value for key, value in meta["cmake"].items() if "FLAGS" in key).lower()
    if any(token in effective_flags for token in ("sanitize", "coverage", "-pg", "profile")):
        raise ValueError("sanitizer/coverage/profiler compile or link flags cannot be timed")
    return binary, meta


def measure(binary, source, name, iterations, env):
    sample_env = dict(env, VPP_HOME=str(source))
    args = [str(binary), "--case", name, "--iterations", str(iterations)]
    raw = command(args, cwd=source, env=sample_env).decode()
    rows = [json.loads(line) for line in raw.splitlines() if line.strip()]
    if len(rows) != 2 or rows[1].get("case") != name:
        raise ValueError(f"unexpected benchmark output: {raw[:500]}")
    return rows[0], rows[1]


def run(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    args.output_created = True
    work = args.work_dir.resolve() if args.work_dir else Path(tempfile.mkdtemp(prefix="vpp-benchmark-"))
    if args.work_dir:
        work.mkdir(parents=True, exist_ok=False)
    # Prevent recursively copying reports/builds into a WORKTREE snapshot.
    if work.is_relative_to(ROOT) and not work.name.startswith(("build-", ".tmp")):
        raise ValueError("work-dir inside the repo must be an ignored build-* directory")
    env = dict(os.environ)
    for key in list(env):
        if key.startswith("VPP_") or key in ("CPATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH"):
            env.pop(key)
    env.update(VPP_ENABLE_JIT="0", VPP_ENABLE_MACHINE_JIT="0", VPP_GC_INTERVAL="256",
               LC_ALL="C", TZ="UTC")
    environment = {key: value for key, value in env.items()
                   if key.startswith("VPP_") or key in ("CC", "CXX", "CFLAGS", "CXXFLAGS", "LDFLAGS",
                                                         "SDKROOT", "MACOSX_DEPLOYMENT_TARGET", "LC_ALL", "TZ")}
    cpu = platform.processor() or platform.machine()
    if sys.platform == "darwin":
        try:
            cpu = command(["sysctl", "-n", "machdep.cpu.brand_string"]).decode().strip()
        except subprocess.CalledProcessError:
            # Restricted macOS hosts may deny this sysctl. Store only chip type,
            # never the serial number/UUID from the hardware overview.
            hardware = json.loads(command(["/usr/sbin/system_profiler", "SPHardwareDataType", "-json"]))
            cpu = hardware["SPHardwareDataType"][0].get("chip_type", cpu)
    elif sys.platform.startswith("linux"):
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                cpu = line.split(":", 1)[1].strip()
                break
    report = {"schema": 1, "protocol": "alternating-independent-processes-v1",
              "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "command": sys.argv, "system": {"os": platform.platform(), "machine": platform.machine(),
                                              "cpu": cpu, "cpu_count": os.cpu_count(), "host": platform.node()},
              "environment": environment, "sample_count": args.samples, "warmup_count": args.warmups,
              "target_ms": args.target_ms, "budget": {"time": 0.10, "rss": 0.15, "gc_p95": 0.15,
                                                      "allocations": 0.15, "noise": 0.05},
              "sources": {}, "builds": {}, "plan": [], "pilots": [], "samples": []}
    # Freeze budgets before building/running either revision.
    write_json(output / "manifest.json", report)
    print(f"Build/snapshot directory: {work}", flush=True)
    paths, binaries = {}, {}
    for side, ref in (("baseline", args.baseline), ("candidate", args.candidate)):
        paths[side] = work / side
        report["sources"][side] = snapshot(ref, paths[side], output)
    # Only the measurement harness is shared. Production code always comes
    # from that side's revision; incompatible baseline APIs cause a build error.
    shared = work / "harness"
    shared.mkdir()
    for name in HARNESS_FILES:
        shutil.copy2(ROOT / "benchmark" / name, shared / name)
    report["harness_sha256"] = tree_digest(shared)
    shutil.copytree(shared, output / "harness")
    tooling = output / "tooling"
    tooling.mkdir()
    for name in ("run_benchmarks.py", "compare_benchmarks.py", "test_benchmark_gate.py"):
        shutil.copy2(Path(__file__).with_name(name), tooling / name)
    report["tooling_sha256"] = tree_digest(tooling)
    for side in paths:
        for name in HARNESS_FILES:
            shutil.copy2(shared / name, paths[side] / "benchmark" / name)
        report["sources"][side]["compiled_source_sha256"] = tree_digest(paths[side])
        print(f"Building {side} {report['sources'][side]['commit'][:12]} (Release)", flush=True)
        binaries[side], report["builds"][side] = build(paths[side], work / (side + "-build"),
                                                     output / (side + "-build.log"), args.jobs, env)
    all_cases = command([str(binaries["baseline"]), "--list"], env=env).decode().splitlines()
    if all_cases != command([str(binaries["candidate"]), "--list"], env=env).decode().splitlines():
        raise ValueError("case catalogs differ")
    names = args.cases.split(",") if args.cases else all_cases
    if len(names) != len(set(names)) or not set(names) <= set(all_cases):
        raise ValueError("unknown/duplicate case selection")
    # Calibrate BASELINE only; freeze one iteration count shared by both sides.
    for name in names:
        meta, record = measure(binaries["baseline"], paths["baseline"], name, 3, env)
        report["pilots"].append({"side": "baseline", "record": record})
        iters = max(1, min(100000, math.ceil(args.target_ms * 1_000_000 / record["ns_per_iteration"])))
        for _ in range(3):
            meta, record = measure(binaries["baseline"], paths["baseline"], name, iters, env)
            report["pilots"].append({"side": "baseline", "record": record})
            if record["ns_per_iteration"] * iters >= args.target_ms * 800000 or iters == 100000:
                break
            iters = min(100000, math.ceil(args.target_ms * 1_000_000 / record["ns_per_iteration"]))
        report["plan"].append({"case": name, "iterations": iters})
        report["builds"]["baseline"]["meta"] = meta
    # Validate compiler/build metadata before collecting the actual samples.
    meta, record = measure(binaries["candidate"], paths["candidate"], names[0], 1, env)
    report["pilots"].append({"side": "candidate", "record": record})
    report["builds"]["candidate"]["meta"] = meta
    if report["builds"]["baseline"]["meta"] != meta:
        raise ValueError("compiler/flags/harness/GC metadata mismatch")
    if meta.get("build") != "Release" or meta.get("coverage") != "OFF":
        raise ValueError("only Release builds without coverage may be measured")
    write_json(output / "manifest.json", {key: value for key, value in report.items() if key != "samples"})
    with (output / "samples.jsonl").open("w") as raw_file:
        for plan in report["plan"]:
            name, iters = plan["case"], plan["iterations"]
            print(f"Measuring {name}: {iters} iterations x {args.samples} processes/version", flush=True)
            for phase, count in (("warmup", args.warmups), ("measure", args.samples)):
                for pair in range(count):
                    order = ("baseline", "candidate") if pair % 2 == 0 else ("candidate", "baseline")
                    for position, side in enumerate(order):
                        meta, record = measure(binaries[side], paths[side], name, iters, env)
                        if meta != report["builds"][side]["meta"]:
                            raise ValueError("metadata changed during measurement")
                        sample = {"side": side, "phase": phase, "pair": pair, "order": position, "record": record}
                        raw_file.write(json.dumps(sample, allow_nan=False) + "\n")
                        raw_file.flush()
                        report["samples"].append(sample)
    write_json(output / "report.json", report)
    result = compare(report)
    write_json(output / "comparison.json", result)
    summary = summarize(result)
    (output / "summary.txt").write_text(summary)
    print(summary, end="", flush=True)
    print(f"Raw report and replay: {output / 'report.json'}", flush=True)
    return {"PASS": 0, "FAIL": 1, "INCONCLUSIVE": 2}[result["status"]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", default="HEAD^1", help="baseline Git revision (default: previous first parent)")
    parser.add_argument("--candidate", default="WORKTREE", help="Git revision or WORKTREE (default)")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--work-dir", type=Path, help="new directory for isolated sources/builds (kept for audit)")
    parser.add_argument("--samples", type=int, default=40)
    parser.add_argument("--warmups", type=int, default=3)
    parser.add_argument("--target-ms", type=float, default=50)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--cases", help="comma-separated subset; omit for full catalog")
    args = parser.parse_args()
    if args.samples < 10 or args.warmups < 3 or args.jobs < 1 or not math.isfinite(args.target_ms) or args.target_ms < 10:
        parser.error("need samples>=10, warmups>=3, jobs>=1, target-ms>=10 (40 samples required for GC p95)")
    try:
        return run(args)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"INVALID: {error}", file=sys.stderr)
        if getattr(args, "output_created", False):
            (args.output / "error.txt").write_text(str(error) + "\n")
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
