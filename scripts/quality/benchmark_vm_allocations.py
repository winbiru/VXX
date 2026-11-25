#!/usr/bin/env python3
"""Compare VM/AOT allocation counters on fixed workloads with a shared runtime.

Set VPP_AOT_VPP_OBJECT, VPP_AOT_PLATFORM_OBJECT and
VPP_AOT_DYNAMIC_BRIDGE_OBJECT to the same runtime objects for all compilers.
The output directory retains exact artifacts, stdout, profiles and a JSON report.
This measures allocation counters; it does not benchmark elapsed time.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess

FIXTURES = (
    "interprocedural_projection_runtime",
    "interprocedural_effect_projection_runtime",
    "direct_effect_projection_runtime",
    "scoped_dispatch_runtime",
)
RUNTIME_OBJECTS = (
    "VPP_AOT_VPP_OBJECT",
    "VPP_AOT_PLATFORM_OBJECT",
    "VPP_AOT_DYNAMIC_BRIDGE_OBJECT",
)
COUNTERS = re.compile(
    rb"^VPP_VM_ALLOC allocations=(\d+) collections=(\d+) free_probes=(\d+)$",
    re.MULTILINE,
)


def identity(path):
    return {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compilers", nargs="+", metavar="LABEL=COMPILER")
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--vm", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    root = args.root.resolve()
    vm = args.vm.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, VPP_HOME=str(root), VPP_OPTIMIZE_IR="1")
    env.pop("VPP_VM_PROFILE", None)
    env.pop("VPP_AOT_CPU_PROFILE", None)
    runtime = {}
    for name in RUNTIME_OBJECTS:
        if not env.get(name):
            parser.error(f"Set {name} to a fixed runtime object")
        path = Path(env[name]).resolve()
        env[name] = str(path)
        runtime[name] = identity(path)
    compilers = {}
    for spec in args.compilers:
        label, sep, path = spec.partition("=")
        if not sep or not re.fullmatch(r"[A-Za-z0-9_-]+", label) or label in compilers:
            parser.error(f"Invalid or duplicate compiler label: {spec}")
        compilers[label] = Path(path).resolve()

    def run(command, run_env, log):
        result = subprocess.run(command, cwd=root, env=run_env,
                                capture_output=True, timeout=args.timeout)
        log.with_suffix(log.suffix + ".stdout").write_bytes(result.stdout)
        log.with_suffix(log.suffix + ".stderr").write_bytes(result.stderr)
        if result.returncode:
            raise RuntimeError(f"{command} failed ({result.returncode}); see {log}.stderr")
        return result

    report = {
        "platform": platform.platform(),
        "machine": platform.machine(),
        "vm": identity(vm),
        "runtime": runtime,
        "compilers": {label: identity(path) for label, path in compilers.items()},
        "fixtures": {},
        "results": [],
        "verified": False,
        "optimizer": True,
        "profile": "VPP_VM_PROFILE=1",
        "aot_options": {key: env[key] for key in (
            "VPP_AOT_NATIVE_MACHO_LINK", "VPP_AOT_NATIVE_ARM64_OBJECT",
            "VPP_AOT_ASSEMBLER") if key in env},
    }
    for name in FIXTURES:
        source = root / "scripts/test/fixtures" / f"{name}.vi"
        expected = root / "scripts/test/expected" / f"{name}.expected"
        report["fixtures"][name] = {"source": identity(source), "expected": identity(expected)}
        for label, compiler in compilers.items():
            for backend, action, suffix in (("vm", "vm-build", ".vmb"), ("aot", "build", ".aot")):
                target = output / f"{label}-{name}{suffix}"
                target.unlink(missing_ok=True)
                run([str(compiler), action, str(source), "-o", str(target)],
                    env, target.with_suffix(suffix + ".build"))
                command = [str(vm), str(target)] if backend == "vm" else [str(target)]
                result = run(command, dict(env, VPP_VM_PROFILE="1"),
                             target.with_suffix(suffix + ".profile"))
                if result.stdout != expected.read_bytes():
                    raise RuntimeError(f"Unexpected output: {label}/{name}/{backend}")
                counts = COUNTERS.findall(result.stderr)
                if len(counts) != 1:
                    raise RuntimeError(f"Missing or ambiguous allocation counters: {target}")
                row = dict(label=label, fixture=name, backend=backend,
                           artifact=identity(target),
                           **dict(zip(("allocations", "collections", "free_probes"),
                                      map(int, counts[0]))))
                report["results"].append(row)
                print(label, name, backend, *(int(value) for value in counts[0]), flush=True)
                (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    # Detect a runtime or source replacement during the comparison.
    for key, record in runtime.items():
        if identity(Path(env[key])) != record:
            raise RuntimeError(f"Runtime changed: {key}")
    if identity(vm) != report["vm"]:
        raise RuntimeError("VM changed during comparison")
    for label, path in compilers.items():
        if identity(path) != report["compilers"][label]:
            raise RuntimeError(f"Compiler changed: {label}")
    for record in report["fixtures"].values():
        for item in record.values():
            if identity(Path(item["path"])) != item:
                raise RuntimeError(f"Fixture changed: {item['path']}")
    report["verified"] = True
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Verified allocation comparison: {output / 'results.json'}")


if __name__ == "__main__":
    main()
