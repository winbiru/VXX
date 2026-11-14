#!/usr/bin/env python3
"""Verify the autonomous compiler's bounded AST session against cold builds."""
import argparse
import os
from pathlib import Path
import platform
import re
import tempfile

from run_native_regression import ROOT, executable_name, execute, normalize


def verify(compiler, env, logs, linux_objects_only=False):
    logs.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="vpp-full-batch-") as tmp:
        work = Path(tmp)
        (work / "shared.vi").write_text(
            "hàm công khai gia_tri() { trả về 40; };\n", encoding="utf-8")
        sources = [work / name for name in ("first.vi", "second.vi")]
        for i, source in enumerate(sources, 2):
            source.write_text(f"nhập shared.*;\nhàm chính() {{ in gia_tri() + {i}; }};\n",
                              encoding="utf-8")
        invalid = work / "invalid.vi"
        invalid.write_text("hàm chính( {\n", encoding="utf-8")

        def call(label, args, cache, *, cold_platform=False, status=0):
            directory = logs / label
            directory.mkdir()
            settings = dict(env, VPP_NATIVE_NO_CACHE="0" if cache else "1",
                            VPP_PREFERENCES_HOME=str(work / label / "preferences"),
                            VPP_COMPILER_PROFILE_FILE=str(directory / "compiler.profile"))
            if cold_platform:
                settings.pop("VPP_AOT_LINUX_OBJECT_DIR", None)
            actual = execute([compiler, *args], ROOT, settings, directory, "build", 900)
            if actual != status:
                raise RuntimeError(f"{label}: expected exit {status}, got {actual}; logs: {directory}")
            return directory

        def outputs(label):
            directory = work / label
            directory.mkdir()
            return [directory / executable_name(name) for name in ("first", "second")]

        def pairs(paths):
            return [arg for pair in zip(sources, paths) for arg in pair]

        def check_markers(directory, count):
            lines = (directory / "build.stdout").read_text(encoding="utf-8").splitlines()
            markers = [line.split() for line in lines if line.startswith("VPP_BATCH_BUILD ")]
            if len(markers) != count or [m[1] for m in markers] != [
                    str(i) for i in range(1, 2 * count, 2)]:
                raise RuntimeError(f"Invalid batch markers: {markers}")

        def check_execution(label, binary, expected):
            run_logs = logs / label
            run_logs.mkdir()
            if execute([binary], work, env, run_logs, "run", 60):
                raise RuntimeError(f"Execution failed: {binary}")
            if normalize((run_logs / "run.stdout").read_bytes()) != expected:
                raise RuntimeError(f"Unexpected output: {binary}")

        cold = outputs("cold")
        if linux_objects_only:
            packaged = outputs("packaged")
            call("cold-0", ["build", sources[0], "-o", cold[0]], False, cold_platform=True)
            call("packaged-0", ["build", sources[0], "-o", packaged[0]], False)
            if cold[0].read_bytes() != packaged[0].read_bytes():
                raise RuntimeError("Cold and packaged Linux platform objects produce different binaries")
            check_execution("run-cold-0", cold[0], b"42\n")
            check_execution("run-packaged-0", packaged[0], b"42\n")
            print("Linux packaged platform object parity OK", flush=True)
            return
        for i, (source, binary) in enumerate(zip(sources, cold)):
            call(f"cold-{i}", ["build", source, "-o", binary], False, cold_platform=True)
        no_cache = outputs("no-cache")
        check_markers(call("no-cache", ["batch-build", *pairs(no_cache)], False), 2)
        warm = outputs("warm")
        warm_logs = call("warm", ["batch-build", *pairs(warm)], True)
        check_markers(warm_logs, 2)
        profile = (warm_logs / "compiler.profile").read_text(encoding="utf-8")
        hits = re.findall(r"source\.ast\.hits (\d+)", profile)
        if len(hits) != 2 or int(hits[0]) != 0 or int(hits[1]) < 1:
            raise RuntimeError(f"Expected AST reuse in second build, got {hits}")
        for i, (reference, uncached, cached) in enumerate(zip(cold, no_cache, warm)):
            if not reference.read_bytes() == uncached.read_bytes() == cached.read_bytes():
                raise RuntimeError(f"Cold and batch binaries differ for {sources[i].name}")
            for label, binary in (("cold", reference), ("no-cache", uncached), ("warm", cached)):
                check_execution(f"run-{label}-{i}", binary, f"{42+i}\n".encode())

        failed = outputs("failed")
        later = failed[0].with_name(executable_name("later"))
        failure_logs = call("failed", ["batch-build", sources[0], failed[0],
                                       invalid, failed[1], sources[1], later], True, status=1)
        check_markers(failure_logs, 1)
        if failed[0].read_bytes() != cold[0].read_bytes() or failed[1].exists() or later.exists():
            raise RuntimeError("Failed batch did not preserve publication boundary")
        print(f"Full compiler batch parity OK; second build AST hits: {hits[1]}", flush=True)
        cold_seconds = sum(float((logs / f"cold-{i}" / "build.seconds").read_text()) for i in range(2))
        no_cache_seconds = float((logs / "no-cache" / "build.seconds").read_text())
        warm_seconds = float((logs / "warm" / "build.seconds").read_text())
        print(f"Probe wall time: cold={cold_seconds:.2f}s, batch/no-cache={no_cache_seconds:.2f}s, "
              f"batch/AST={warm_seconds:.2f}s (one sample per mode)", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--clang", type=Path, help="Windows package toolchain")
    parser.add_argument("--logs", required=True, type=Path)
    parser.add_argument("--linux-objects-only", action="store_true",
                        help="verify packaged Linux objects without the experimental batch probe")
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)
    env = dict(os.environ)
    if platform.system() == "Windows":
        if args.clang is None:
            parser.error("--clang required on Windows")
        from check_windows_full_bootstrap import package_env
        env = package_env(compiler.parent.parent, args.clang.resolve(strict=True))
    installed = compiler.parent.parent / "share/vpp"
    env.setdefault("VPP_HOME", str(installed if (installed / "gói").is_dir() else ROOT))
    if args.linux_objects_only and (platform.system() != "Linux" or not env.get("VPP_AOT_LINUX_OBJECT_DIR")):
        parser.error("--linux-objects-only requires Linux and VPP_AOT_LINUX_OBJECT_DIR")
    verify(compiler, env, args.logs.resolve(), args.linux_objects_only)


if __name__ == "__main__":
    main()
