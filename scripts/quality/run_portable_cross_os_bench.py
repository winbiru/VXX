#!/usr/bin/env python3
"""Build matched C/C++/Rust references and benchmark the V++ VM on any host OS.

The VM benchmark is intentionally separate from the V++ native AOT/i32
benchmark: native runtime support varies by OS. All runners validate the same
16 input answers and their accumulated checksum before accepting a sample.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
from datetime import datetime, timezone


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "benchmark" / "languages"
CASES = ["arithmetic", "branch_loop", "fibonacci", "list_128", "unicode_replace", "json_parse"]


def run(*args):
    print("+", " ".join(str(arg) for arg in args), flush=True)
    subprocess.run(list(map(str, args)), check=True, cwd=ROOT)


def version(command, *args):
    completed = subprocess.run([str(command), *args], cwd=ROOT,
                               capture_output=True, text=True, check=False)
    return (completed.stdout + completed.stderr).strip()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--vpp-cli", type=Path, required=True)
    p.add_argument("--output-dir", type=Path, required=True)
    p.add_argument("--runs", type=int, default=5)
    p.add_argument("--target-ms", type=float, default=30)
    p.add_argument("--environment-label", default="",
                   help="E.g. GitHub hosted Windows x64, Linux ARM64 in Docker")
    args = p.parse_args()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    vpp = args.vpp_cli.resolve()
    if not vpp.is_file():
        p.error(f"V++ CLI not found: {vpp}")

    msvc = platform.system() == "Windows"
    if msvc:
        cxx = os.environ.get("CXX", "cl")
        cc = os.environ.get("CC", "cl")
        if not shutil.which(cxx) or not shutil.which(cc):
            p.error("MSVC cl not found; initialize a Visual Studio developer command prompt")
        def compile_source(source, object_file, is_c=False, numeric=False):
            flags = ["/nologo", "/O2", "/DNDEBUG", "/utf-8", "/c"]
            if numeric:
                flags.append("/DVPP_NUMERIC_KERNELS")
            flags.append("/TC" if is_c else "/TP")
            run(cc if is_c else cxx, *flags, source, f"/Fo{object_file}")

        ext = ".obj"
        driver, workloads, ndriver, cwork = [out / (x + ext)
                                             for x in ("driver", "workloads", "numeric-driver", "c-workloads")]
        compile_source(SOURCE / "driver.cpp", driver)
        compile_source(SOURCE / "workloads.cpp", workloads)
        run(cxx, "/nologo", driver, workloads, f"/Fe{out / 'cpp.exe'}")
        compile_source(SOURCE / "driver.cpp", ndriver, numeric=True)
        compile_source(SOURCE / "workloads.c", cwork, is_c=True)
        run(cxx, "/nologo", ndriver, cwork, f"/Fe{out / 'c.exe'}")
        cpp, c = out / "cpp.exe", out / "c.exe"
    else:
        cxx = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
        cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("gcc")
        if not cxx or not cc:
            p.error("C/C++ compilers not found")
        driver, workloads, ndriver, cwork = [out / (x + ".o")
                                             for x in ("driver", "workloads", "numeric-driver", "c-workloads")]
        run(cxx, "-std=c++17", "-O3", "-DNDEBUG", "-c", SOURCE / "driver.cpp", "-o", driver)
        run(cxx, "-std=c++17", "-O3", "-DNDEBUG", "-c", SOURCE / "workloads.cpp", "-o", workloads)
        run(cxx, driver, workloads, "-o", out / "cpp")
        run(cxx, "-std=c++17", "-O3", "-DNDEBUG", "-DVPP_NUMERIC_KERNELS", "-c", SOURCE / "driver.cpp", "-o", ndriver)
        run(cc, "-std=c11", "-O3", "-fwrapv", "-c", SOURCE / "workloads.c", "-o", cwork)
        run(cxx, ndriver, cwork, "-o", out / "c")
        cpp, c = out / "cpp", out / "c"

    manifest = {"schema": 1, "root": str(ROOT), "runtimes": []}
    def add(name, exe, arglist, cases):
        manifest["runtimes"].append({"name": name, "executable": str(exe),
                                      "arguments": list(map(str, arglist)),
                                      "cases": cases, "environment": {}})

    add("vpp-vm", vpp, ["run", "--compiler=cpp", SOURCE / "workloads.vi"], CASES)
    add("cpp", cpp, [], CASES[:5])
    add("c", c, [], CASES[:3])
    rustc = shutil.which("rustc")
    if rustc:
        rust_obj = out / ("rust-workloads.obj" if msvc else "rust-workloads.o")
        rust_exe = out / ("rust.exe" if msvc else "rust")
        run(rustc, "--edition=2024", "--crate-type=lib", "--emit=obj",
            "-C", "opt-level=3", "-C", "overflow-checks=no",
            "-C", "lto=off", "-C", "panic=abort",
            SOURCE / "workloads.rs", "-o", rust_obj)
        if msvc:
            run(cxx, "/nologo", ndriver, rust_obj, f"/Fe{rust_exe}")
        else:
            run(cxx, ndriver, rust_obj, "-o", rust_exe)
        add("rust", rust_exe, [], CASES[:3])
    add("python", sys.executable, ["-B", "-c", (SOURCE / "python-inline.txt").read_text(encoding="utf-8")], CASES)
    node = shutil.which("node")
    if node:
        add("node", node, [SOURCE / "workloads.js"], CASES)
    java, javac = shutil.which("java"), shutil.which("javac")
    if java and javac:
        java_out = out / "java"
        java_out.mkdir(exist_ok=True)
        run(javac, "-encoding", "UTF-8", "-d", java_out, SOURCE / "Bench.java")
        add("java", java, ["-cp", java_out, "Bench"], CASES[:5])
    ruby = shutil.which("ruby")
    if ruby:
        add("ruby", ruby, [SOURCE / "workloads.rb"], CASES)

    revision = (version("git", "rev-parse", "HEAD") if shutil.which("git") else
                os.environ.get("VPP_BENCH_REVISION", "unknown"))
    manifest.update(
        revision=revision,
        dirty=(bool(version("git", "status", "--porcelain")) if shutil.which("git") else None),
        created_utc=datetime.now(timezone.utc).isoformat(),
        host={"system": platform.system(), "release": platform.release(),
              "version": platform.version(), "machine": platform.machine(),
              "processor": platform.processor()},
        toolchain={"cc": version(cc, "/Bv" if msvc else "--version"),
                   "cxx": version(cxx, "/Bv" if msvc else "--version"),
                   "rust": version(rustc, "--version") if rustc else None,
                   "python": platform.python_version(),
                   "node": version(node, "--version") if node else None,
                   "java": version(java, "-version") if java else None,
                   "ruby": version(ruby, "--version") if ruby else None},
        source_sha256={source.name: hashlib.sha256(source.read_bytes()).hexdigest()
                       for source in sorted(SOURCE.iterdir()) if source.is_file()},
    )

    manifest_path = out / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    names = ",".join(runtime["name"] for runtime in manifest["runtimes"])
    run(sys.executable, ROOT / "scripts" / "quality" / "compare_languages_aot.py",
        "--manifest", manifest_path, "--runtimes", names,
        "--environment-label", args.environment_label or
            f"{platform.system()} {platform.machine()} portable V++ C++ VM",
        "--runs", str(args.runs), "--target-ms", str(args.target_ms),
        "--output", out / "results.json")
    print("Evidence:", out / "results.json")


if __name__ == "__main__":
    main()
