#!/usr/bin/env python3
"""CLI-level hardening checks for the V++ 1.0 RC gate.

The repository intentionally no longer carries the old C++ unit/smoke test tree.  This
script keeps the RC checks that can be verified through the public compiler boundary:
malformed/fuzzed source must not crash, compiler output must be deterministic, and larger
single-file/module projects must compile within a bounded time.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import time

try:
    import resource
except ImportError:  # Windows
    resource = None


SEED = 0x56505031


def repository_root() -> Path:
    return Path(__file__).resolve().parents[2]


def find_vpp(root: Path, configured: str | None) -> Path:
    if configured:
        candidate = Path(configured).expanduser().resolve()
        if candidate.is_file():
            return candidate
        raise SystemExit(f"Không tìm thấy V++ executable: {candidate}")

    candidates = [
        root / "cmake-build-debug/bin/vpp-cli",
        root / "build/bin/vpp-cli",
        root / "build/bin/Release/vpp-cli.exe",
        root / "build/Release/vpp-cli.exe",
        root / "build/vpp-cli.exe",
        root / "bin/vpp-cli",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise SystemExit("Không tìm thấy V++ executable; truyền --vpp <đường-dẫn>")


def run_cli(
    vpp: Path,
    args: list[str],
    *,
    cwd: Path,
    timeout: float,
    accepted: set[int] | None = None,
) -> subprocess.CompletedProcess[bytes]:
    try:
        result = subprocess.run(
            [str(vpp), *args],
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            env=dict(os.environ, VPP_HOME=str(repository_root())),
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"timeout sau {timeout:.1f}s: {' '.join(args)}") from error

    if result.returncode < 0:
        raise RuntimeError(f"V++ bị signal {-result.returncode}: {' '.join(args)}")
    if accepted is not None and result.returncode not in accepted:
        stderr = result.stderr.decode("utf-8", errors="replace")[-2000:]
        raise RuntimeError(
            f"exit code {result.returncode}, mong đợi {sorted(accepted)}: {' '.join(args)}\n{stderr}"
        )
    return result


def malformed_corpus() -> list[bytes]:
    return [
        b"",
        "hàm".encode(),
        "hàm main(".encode(),
        "hàm main() {".encode(),
        "nếu (".encode(),
        b'"chuoi chua dong',
        b"[1, 2, 3",
        b'{"a": 1',
        "lớp A { hàm f() {".encode(),
        "giao diện A { hàm f( ; }".encode(),
        "thử { ném 1; }".encode(),
        "nhập ;".encode(),
        b"main = (((((((1;",
        bytes([0xC0, 0xAF]),  # overlong UTF-8
        bytes([0xED, 0xA0, 0x80]),  # UTF-8 surrogate
        bytes([0xF4, 0x90, 0x80, 0x80]),  # > U+10FFFF
    ]


def generated_fuzz_cases(count: int) -> list[bytes]:
    rng = random.Random(SEED)
    ascii_pool = (
        b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        b"{}()[];,:.+-*/%=!<>_ \n\t\""
    )
    cases: list[bytes] = []
    for _ in range(count):
        length = rng.randint(0, 256)
        payload = bytearray()
        for _ in range(length):
            if rng.random() < 0.88:
                payload.append(ascii_pool[rng.randrange(len(ascii_pool))])
            else:
                payload.append(rng.randrange(256))
        cases.append(bytes(payload))
    return cases


def check_frontend_fuzz(vpp: Path, work: Path, count: int) -> None:
    corpus = malformed_corpus() + generated_fuzz_cases(count)
    fuzz_dir = work / "fuzz"
    fuzz_dir.mkdir()
    started = time.monotonic()
    for index, source in enumerate(corpus):
        path = fuzz_dir / f"case-{index:04d}.vi"
        path.write_bytes(source)
        run_cli(vpp, ["dựng", str(path)], cwd=work, timeout=5.0, accepted={0, 1})
    elapsed = time.monotonic() - started
    print(f"frontend fuzz: {len(corpus)} cases, seed=0x{SEED:08X}, {elapsed:.2f}s")


def check_determinism(vpp: Path, work: Path) -> None:
    project = work / "deterministic"
    project.mkdir()
    (project / "math.vi").write_text(
        "hàm công khai gấpĐôi(x) { trả về x * 2; };\n",
        encoding="utf-8",
    )
    main = project / "main.vi"
    main.write_text(
        "nhập math;\n"
        "hàm main() {\n"
        "    giáTrị = gấpĐôi(21);\n"
        "    in giáTrị;\n"
        "};\n",
        encoding="utf-8",
    )

    for mode in ("--dump-ast", "--dump-ir", "--giải-mã"):
        outputs: list[bytes] = []
        for _ in range(3):
            result = run_cli(vpp, [mode, str(main)], cwd=project, timeout=10.0, accepted={0})
            outputs.append(result.stdout + b"\n---stderr---\n" + result.stderr)
        if outputs[1:] != outputs[:-1]:
            raise RuntimeError(f"compiler output không deterministic ở mode {mode}")
    print("determinism: AST/IR/disassembly stable across 3 runs")


def make_stress_source(function_count: int) -> str:
    lines = [f"hàm f{i}(x) {{ trả về x + {i}; }};" for i in range(function_count)]
    lines.extend(
        [
            "hàm main() {",
            f"    in f{function_count - 1}(1);",
            "};",
        ]
    )
    return "\n".join(lines) + "\n"


def child_peak_rss_mb() -> float | None:
    if resource is None:
        return None
    peak = float(resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)
    if sys.platform == "darwin":
        return peak / (1024.0 * 1024.0)
    return peak / 1024.0


def check_stress(
    vpp: Path,
    work: Path,
    function_count: int,
    module_count: int,
    max_rss_mb: float,
) -> None:
    stress = work / "stress"
    stress.mkdir()
    single = stress / "single.vi"
    single.write_text(make_stress_source(function_count), encoding="utf-8")

    started = time.monotonic()
    run_cli(vpp, ["dựng", str(single)], cwd=stress, timeout=30.0, accepted={0})
    single_elapsed = time.monotonic() - started

    module_dir = stress / "modules"
    module_dir.mkdir()
    imports: list[str] = []
    functions_per_module = 24
    for module_index in range(module_count):
        name = f"m{module_index}"
        imports.append(f"nhập {name};")
        body = "\n".join(
            f"hàm công khai {name}_f{fn}(x) {{ trả về x + {fn}; }};"
            for fn in range(functions_per_module)
        )
        (module_dir / f"{name}.vi").write_text(body + "\n", encoding="utf-8")
    root = module_dir / "main.vi"
    root.write_text("\n".join(imports + ["hàm main() { in 1; };"]) + "\n", encoding="utf-8")

    started = time.monotonic()
    for _ in range(5):
        run_cli(vpp, ["dựng", str(root)], cwd=module_dir, timeout=30.0, accepted={0})
    module_elapsed = time.monotonic() - started
    peak_rss = child_peak_rss_mb()
    if peak_rss is not None and max_rss_mb > 0 and peak_rss > max_rss_mb:
        raise RuntimeError(
            f"compiler peak RSS {peak_rss:.1f} MiB vượt giới hạn {max_rss_mb:.1f} MiB"
        )
    rss_text = "n/a" if peak_rss is None else f"{peak_rss:.1f} MiB"
    print(
        f"stress: {function_count} functions={single_elapsed:.2f}s; "
        f"{module_count} modules x {functions_per_module} functions x5={module_elapsed:.2f}s; "
        f"peak RSS={rss_text}"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--vpp", help="đường dẫn tới vpp-cli/vpp-cli.exe")
    parser.add_argument("--fuzz-cases", type=int, default=512)
    parser.add_argument("--stress-functions", type=int, default=800)
    parser.add_argument("--stress-modules", type=int, default=24)
    parser.add_argument("--max-rss-mb", type=float, default=1024.0)
    args = parser.parse_args()
    if args.fuzz_cases < 0 or args.stress_functions < 1 or args.stress_modules < 1:
        parser.error("các số lượng test phải không âm/dương hợp lệ")

    root = repository_root()
    vpp = find_vpp(root, args.vpp)
    print(f"V++ RC hardening: {vpp}")
    with tempfile.TemporaryDirectory(prefix="vpp-rc-hardening-") as temp:
        work = Path(temp)
        check_frontend_fuzz(vpp, work, args.fuzz_cases)
        check_determinism(vpp, work)
        check_stress(vpp, work, args.stress_functions, args.stress_modules, args.max_rss_mb)
    print("RC hardening: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
