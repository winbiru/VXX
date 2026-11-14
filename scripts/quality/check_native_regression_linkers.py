#!/usr/bin/env python3
"""Run the independent historical linker oracles against a packaged compiler."""
import os
from pathlib import Path
import subprocess
import sys
from check_native_autonomous_selfhost import (
    run, macho_reader_smoke, layout_smoke, patch_smoke, executable_smoke,
    direct_object_smoke,
)

ROOT = Path(__file__).resolve().parents[2]


def main():
    phase, compiler_arg, output_arg = sys.argv[1:]
    compiler, out = Path(compiler_arg).resolve(), Path(output_arg).resolve()
    env = dict(os.environ)
    if phase == "reader":
        macho_reader_smoke(compiler, ROOT, out, env)
    elif phase == "direct-object":
        direct_object_smoke(compiler, ROOT, out, env)
    elif phase in ("layout", "patch", "executable"):
        oracle = {"layout": layout_smoke, "patch": patch_smoke, "executable": executable_smoke}[phase]
        probe = out / ("native_macho_" + phase)
        run([compiler, "build", ROOT / "scripts/test/fixtures" / (probe.name + ".vi"), "-o", probe], env)
        oracle(probe, out, env)
    elif phase == "elf-partial":
        env["VPP_NATIVE_ELF_LINK"] = "1"
        probes = []
        for name in ("native_elf_partial_linker_archive", "native_elf_linker_archive"):
            probe = out / name
            run([compiler, "build", ROOT / "scripts/test/fixtures" / (name + ".vi"), "-o", probe], env)
            subprocess.run(["bash", str(ROOT / "scripts/quality/audit_linux_static_elf.sh"), str(probe)], check=True, env=env)
            probes.append(str(probe))
        subprocess.run([sys.executable, str(ROOT / "scripts/quality/check_native_elf_partial_linker.py"), *probes, str(out / "elf-regressions")], check=True, env=env)
    else:
        raise ValueError(f"unknown linker regression: {phase}")


if __name__ == "__main__":
    main()
