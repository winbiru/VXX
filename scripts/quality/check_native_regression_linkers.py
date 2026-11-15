#!/usr/bin/env python3
"""Run the independent historical linker oracles against a packaged compiler."""
import os
from pathlib import Path
import subprocess
import sys
from check_native_autonomous_selfhost import (
    run, macho_reader_smoke, layout_smoke, patch_smoke, executable_smoke,
    direct_object_smoke, prepare_probe,
)

ROOT = Path(__file__).resolve().parents[2]


def main():
    args = sys.argv[1:]
    jit_mode = args[:1] == ["--jit"]
    if jit_mode:
        args = args[1:]
    phase, executable_arg, output_arg = args
    executable, out = Path(executable_arg).resolve(), Path(output_arg).resolve()
    compiler, jit = (None, executable) if jit_mode else (executable, None)
    env = dict(os.environ)
    if phase == "reader":
        macho_reader_smoke(compiler, ROOT, out, env, jit=jit)
    elif phase == "direct-object":
        direct_object_smoke(compiler, ROOT, out, env, jit=jit)
    elif phase in ("layout", "patch", "executable"):
        oracle = {"layout": layout_smoke, "patch": patch_smoke, "executable": executable_smoke}[phase]
        probe = prepare_probe(compiler, ROOT, out, env, "native_macho_" + phase, jit=jit)
        oracle(probe, out, env)
    elif phase == "elf-partial":
        env["VPP_NATIVE_ELF_LINK"] = "1"
        if jit_mode:
            # Inspect the packaged host; the V++ linker itself runs from JIT
            # memory and its synthetic ELF outputs remain oracle artifacts.
            subprocess.run(["/bin/bash", str(ROOT / "scripts/quality/audit_linux_static_elf.sh"), str(jit)],
                           check=True, env=dict(env, PATH="/usr/bin:/bin"))
            subprocess.run([sys.executable, str(ROOT / "scripts/quality/check_native_elf_partial_linker.py"),
                            "--jit", str(jit), str(out / "elf-regressions")], check=True, env=env)
            return
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
