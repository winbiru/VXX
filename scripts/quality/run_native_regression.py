#!/usr/bin/env python3
"""Run V++ source regressions in JIT, or explicitly exercise the native toolchain."""
import argparse
import collections
import concurrent.futures
import contextlib
import hashlib
import threading
import json
import math
import os
import platform
import re
import signal
import struct
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "scripts/test"
MANIFEST = TESTS / "regression.json"
FIXTURE_EXECUTABLE = None

# Explicitly reviewed positive fixtures. Entrypoint, compiler, import-resolution,
# diagnostic and other context-sensitive probes remain independent executables.
# Each suite has at most nine entries so the dispatcher uses one ASCII digit.
MODULE_SUITES = {
    "core": ("bo_qua", "boolean", "ngoai_le", "nhanh_dieu_kien_moi",
             "so_thuc", "thoat_vong_lap", "toan_tu_moi"),
    "stdlib": ("stdlib", "stdlib_tinh_toan", "stdlib_mo_rong", "bo_noi_chuoi",
               "stdlib_nen_tang", "gia_tri", "stdlib_crypto"),
    "text": ("codec_uuid_bitset", "quet_tach_chuoi", "quoc_te_hoa"),
    "json": ("json_phan_tich", "json_dieu_khien", "json_escape_span",
             "json_key_scan", "json_utf8", "json_byte_view", "json_integer_overflow_scan"),
    "network": ("ffi_socket", "goi_dau_noi_mang", "goi_http",
                "stdlib_http", "stdlib_http_post_put"),
    "files": ("ffi_tep", "ffi_thu_muc", "loi_tep_co_cau_truc", "file_dem",
              "file_stream_oop", "stdlib_io_config_time"),
    "collections": ("collections_oop", "ngan_xep_oop"),
    "application": ("goi_dung", "nhat_ky", "stdlib_starter"),
    "time": ("goi_thoi_gian", "goi_thoi_gian_1_0"),
}
HARNESS_KEYS = ("dump", "linker_phase", "vm_phase", "cmake")
JIT_I32_HARNESSES = {
    "checknativeprint.cmake", "checknativebulk.cmake", "checknativebytefile.cmake",
    "checknativebytesops.cmake", "checknativeread.cmake",
    "checknativelexer.cmake", "checknativeparser.cmake", "checknativecodegen.cmake",
}
# The legacy mixed backend uses this allowlist for reviewed JIT fixtures.
JIT_VM_PHASES = {"core", "directory", "system", "process", "resolver", "socket", "socket_negative", "tls"}
JIT_CASE_KEYS = {"id", "suite", "source", "expected", "expected_stderr", "stdout", "exit", "smoke", "jit"}

# Reviewed, self-contained programs with no imports or filesystem dependencies.
# Pin the reviewed source contents so edited fixtures fall back to private copies.
# Normalize line endings for Windows checkouts.
FIXTURE_FREE_SOURCES = {
    "scripts/test/fixtures/anonymous_functions/entry.vi": "f974f9a59d7ce32cf77dacd5ea5de93ea527dfcdfa5bfc23541ce4cb71e962aa",
    "scripts/test/kiem_tra_bo_qua.vi": "218d0c7d1b5157e1beafc51f77d23a0c980392aaa084c1bc08558d32b53d6ab3",
    "scripts/test/kiem_tra_boolean.vi": "e6d7898243106c450555a6a77e468ba9722b302f4ee9853809b530c2c36eb091",
    "scripts/test/kiem_tra_chon_ca.vi": "a2d6119bb2e841c66ff3426a9d572efe82b142f4bb1294460256933767acc93e",
    "scripts/test/kiem_tra_closure_capture.vi": "7cf4a63b7015f9a28c6a9acc0e273d2cf78ffc503c1d5b7f2b8358639a3ec77c",
    "scripts/test/kiem_tra_constructor_tham_so.vi": "89b60f1fa693505210472b8ba1ab4d9486aae42ebb1b9e92020ee7d2f1313d4e",
    "scripts/test/kiem_tra_de_quy.vi": "2d3eac2ae4880898859386192c430ba178ab72c1f49ca076e7edad077c30b299",
    "scripts/test/kiem_tra_dieu_kien_phu_dinh.vi": "d7b144d944a3480e5bad10821ba3b9e308a64597bf1a4f2c8e3ed2302c5b8474",
    "scripts/test/kiem_tra_ham.vi": "4e2323e821d89420572d9f4801b5986028f4683ed9148a70eeaa3c7b622c26a0",
    "scripts/test/kiem_tra_khi.vi": "1c932c5bbcf0c12e4398931f085733ecf421743afe484438a464c561e8e72dff",
    "scripts/test/kiem_tra_noi_chuoi.vi": "021e3eb52e22480a8a2b164510dfcbe3e791aaecef8b537c3c7d6438cc6890c3",
    "scripts/test/kiem_tra_so_chan_1-20.vi": "fb666c3b8188da3b332ccf1a3a225cdfda3695a0ef30bde02045c89aec3a6fb9",
    "scripts/test/kiem_tra_so_nguyen.vi": "3ee00268ab56251ee1c936a6c8de67cde7229617f309574399fe0646b8fee5f6",
    "scripts/test/kiem_tra_so_thuc.vi": "27e9f6a10d806daebae1c1115762b313ca713ae1e77492b3530d677064952614",
    "scripts/test/kiem_tra_thoat_vong_lap.vi": "6b53b3989a5ea7c11a9aba2ca60344abc3ff5ea8dd76cd04e5f6d4aa5bc496e9",
    "scripts/test/kiem_tra_toan_tu_bit.vi": "c5910101002f04846fb80eefa56175af1561f1655eee7d75bf43111d7198a407",
    "scripts/test/kiem_tra_toan_tu_moi.vi": "2b0d1eb5096349f702ca873a6312677e5219853ffee42182c987e138f52c2460",
    "scripts/test/kiem_tra_tra_ve.vi": "026f5240ba27e10e58dfe326548dfa872908849c905a92b318fc10adf4faf0b5",
}


def fixture_free(case, source):
    if case.get("suite") != "full" or set(case) - JIT_CASE_KEYS:
        return False
    digest = FIXTURE_FREE_SOURCES.get(case["source"])
    return digest is not None and hashlib.sha256(
        source.read_bytes().replace(b"\r\n", b"\n")).hexdigest() == digest


def toolchain_reason(case, *, jit_mode=False):
    """Cases whose contract requires a specific compiler or executable."""
    if jit_mode and ("dump" in case or case.get("cmake") in JIT_I32_HARNESSES or
                     case.get("cmake") == "check_native_runtime.cmake" or
                     case.get("linker_phase") in {"reader", "layout", "patch", "executable", "direct-object", "elf-partial"} or
                     case.get("vm_phase") in JIT_VM_PHASES):
        return None
    if any(key in case for key in HARNESS_KEYS):
        return "compiler/linker/VM artifact assertion"
    if case.get("tooling_test"):
        if jit_mode and case["id"] in {"kiem_tra_chia_nhom_golden", "kiem_tra_do_thi_build", "kiem_tra_regression_runner"}:
            return None
        return "build tooling and executable fixture"
    if case.get("dns_mock") and not jit_mode:
        return "DNS fixture requires the native i32 socket ABI"
    return None


def jit_eligible(case, experimental=False):
    """Explicit JIT mode runs every source case outside the native toolchain."""
    return not toolchain_reason(case, jit_mode=experimental) and (
        experimental or case.get("jit", False) or case.get("compile_reject", False))


def jit_environment(env):
    env = dict(env, VPP_AOT_ASSEMBLER="/assembler-must-not-run",
               VPP_AOT_LINKER="/linker-must-not-run")
    env["PATH"] = str(Path(os.environ["SystemRoot"]) / "System32") if os.name == "nt" else ""
    env.pop("VPP_JIT_PROFILE", None)
    env.pop("VPP_JIT_TIER", None)
    return env


def module_source(case):
    """Adapt a reviewed function-only fixture without merging its imports."""
    if case.get("suite") != "full" or set(case) - {
            "id", "suite", "source", "expected", "http", "smoke", "jit"} or "expected" not in case:
        return None
    source = (ROOT / case["source"]).read_text(encoding="utf-8")
    # Only imports/comments before the entry function; no argument or relative
    # import dependency. The allowlist is intentional, not automatic discovery.
    prefix = re.match(r"(?m)\A(?:\s|//[^\n]*\n|nhập [^;\n]+;)*", source)
    entry = re.match(r"hàm chính\(\)\s*\{", source[prefix.end():])
    # Ignore comments and quoted literals when checking the function body.
    # Keep the original source intact for compilation and diagnostics.
    code = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  ' ', source, flags=re.S)
    if (not entry or code.count("hàm chính()") != 1
            or re.search(r"nhập\s+\.|đối số|đường dẫn thực thi|trả về", code)):
        return None
    body = code[code.index("hàm chính()") + len("hàm chính()"):].lstrip()
    depth = 0
    for i, char in enumerate(body):
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                if not re.fullmatch(r"\s*;?\s*", body[i + 1:]):
                    return None
                break
    else:
        return None
    start = prefix.end()
    return source[:start] + source[start:].replace(
        "hàm chính()", "hàm công khai kiểm thử()", 1)


def plan_module_suites(cases):
    by_id = {case["id"]: case for case in cases}
    if len(by_id) != len(cases):
        raise ValueError("duplicate regression IDs in module selection")
    suites = {}
    seen = set()
    for name, members in MODULE_SUITES.items():
        if not 2 <= len(members) <= 9 or len(set(members)) != len(members) or seen.intersection(members):
            raise ValueError(f"invalid module suite membership: {name}")
        seen.update(members)
        selected = []
        for suffix in members:
            case = by_id.get("kiem_tra_" + suffix)
            if case is not None:
                source = module_source(case)
                if source is not None:
                    selected.append((case, source))
        if len(selected) >= 2:
            suites[name] = selected
    return suites


def build_module_suite(name, members, compiler, output, logs, env, timeout):
    """Compile one executable, dispatched in a new process for every case."""
    logs.mkdir(parents=True, exist_ok=True)
    imports, branches = [], []
    for i, (case, source) in enumerate(members):
        (logs / f"case{i}.vi").write_text(source, encoding="utf-8")
        imports.append(f"nhập case{i} như ca{i};")
        branches.append(f"    nếu (k == {48 + i}) {{ ca{i}.kiểm thử(); trả về 0; }};")
    entry = logs / "suite.vi"
    entry.write_text("\n".join(imports + [
        "hàm chính() {",
        "    nếu (đối số đếm() != 2) { trả về 97; };",
        "    h = đối số lấy(1);",
        "    nếu (byte dài(h) != 1) { byte hủy(h); trả về 97; };",
        "    k = byte lấy(h, 0); byte hủy(h);",
        *branches, "    trả về 97;", "};", "",
    ]), encoding="utf-8")
    (logs / "members.json").write_text(json.dumps(
        [case["id"] for case, _ in members], ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"BUILD MODULE {name}: {len(members)} cases, one executable", flush=True)
    module_env = dict(env, VPP_PREFERENCES_HOME=str(logs / "preferences"))
    if module_env.get("VPP_COMPILER_PROFILE_FILE"):
        module_env["VPP_COMPILER_PROFILE_FILE"] = str(logs / "compiler.profile")
    status = build(compiler, entry, output, ROOT, module_env, logs, timeout * len(members))
    if status or not output.is_file():
        raise RuntimeError(f"module {name}: build exit {status} or missing executable; logs: {logs}")


def normalize(data):
    return data.replace(b"\r\n", b"\n")


def normalize_stderr(data):
    # The runner executes in an isolated temporary directory, so source
    # diagnostics legitimately contain absolute repository paths. Normalize
    # only the two source path fields of a stack frame, retaining the full
    # diagnostic, function name, line, column and module identity.
    prefixes = {str(ROOT).encode("utf-8") + os.sep.encode(),
                ROOT.as_posix().encode("utf-8") + b"/"}

    def relative(path):
        for prefix in prefixes:
            if path.startswith(prefix):
                return path[len(prefix):].replace(b"\\", b"/")
        return path

    def frame(match):
        return (match[1] + relative(match[2]) + match[3] +
                relative(match[4]) + match[5])

    pattern = r"(?m)^(  ở [^\n]+? \()([^\n]+?)(:\d+:\d+\) \[mô đun=)([^\n]+)(\])$"
    return re.sub(pattern.encode("utf-8"), frame, normalize(data))


def executable_name(name):
    return name + ".exe" if platform.system() == "Windows" else name


def partition(cases, count, module_suites=False):
    # Historical timings are scheduling hints only; the new backend can differ.
    weights = {}
    for line in (ROOT / "scripts/quality/native_golden_costs.tsv").read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.startswith("#"):
            name, weight = line.split("\t")
            weights[name] = int(weight)
    def cost(case):
        if "pending" in case:
            return 0
        return case.get("weight_ms", weights.get(case["id"], 2000))
    shards = [[] for _ in range(count)]
    totals = [0] * count
    for case in cases:
        if case.get("http"):
            shards[0].append(case)
            totals[0] += cost(case)
    # Keep reviewed module executables intact across CI shards. Otherwise
    # sharding turns a single shared build back into several independent ones.
    units = []
    grouped = set()
    if module_suites and count > 1:
        for members in plan_module_suites(cases).values():
            group = [case for case, _ in members]
            grouped.update(c["id"] for c in group)
            if any(c.get("http") for c in group):
                for c in group:
                    if not c.get("http"):
                        shards[0].append(c)
                        totals[0] += cost(c)
            else:
                units.append(group)
    units.extend([c] for c in cases if not c.get("http") and c["id"] not in grouped)
    for unit in sorted(units, key=lambda u: (-sum(map(cost, u)), u[0]["id"])):
        index = min(range(count), key=lambda i: (totals[i], len(shards[i]), i))
        shards[index].extend(unit)
        totals[index] += sum(map(cost, unit))
    return shards


def load_manifest():
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    cases = manifest["cases"]
    ids = [case["id"] for case in cases]
    if len(ids) != len(set(ids)):
        raise ValueError("duplicate regression IDs")
    for case in cases:
        if case.get("suite") not in ("full", "i32", "host", "guest"):
            raise ValueError(f"unknown regression suite: {case['id']}: {case.get('suite')}")
        if case["suite"] in ("host", "guest") and case.get("smoke"):
            raise ValueError(f"backend-specific regression cannot be a mixed smoke case: {case['id']}")
        if "expected_stderr" in case:
            if not isinstance(case["expected_stderr"], str) or not case["expected_stderr"].endswith(".expected"):
                raise ValueError(f"invalid stderr golden path: {case['id']}")
            if "diagnostic" in case or "run_diagnostic_regex" in case:
                raise ValueError(f"ambiguous stderr golden contract: {case['id']}")
            if ("expected" in case) == ("stdout" in case):
                raise ValueError(f"stderr golden requires exactly one stdout contract: {case['id']}")
    for kind, registered in (
        (".vi", {case["source"] for case in cases} | {source for case in cases for source in case.get("sources", [])} | set(manifest["unregistered_sources"])),
        (".expected", {case[key] for case in cases for key in ("expected", "expected_stderr", "historical_expected") if key in case}),
    ):
        actual = {p.relative_to(ROOT).as_posix() for p in TESTS.rglob("*" + kind)}
        actual = {p for p in actual if not any(part.startswith(".") for part in Path(p).parts)}
        if actual != registered:
            raise ValueError(f"{kind} inventory mismatch: new={sorted(actual-registered)}, missing={sorted(registered-actual)}")
    for case in cases:
        if case.get("jit") and (case["suite"] != "full" or set(case) - JIT_CASE_KEYS):
            raise ValueError(f"unsupported JIT case configuration: {case['id']}")
    full = [c for c in cases if c["suite"] == "full"]
    shards = partition(full, 1)
    assigned = [c["id"] for shard in shards for c in shard]
    if (sorted(assigned) != sorted(c["id"] for c in full)
            or any(not any("pending" not in c for c in shard) for shard in shards)
            or any(c.get("http") for shard in shards[1:] for c in shard)):
        raise ValueError("invalid full regression shard assignment")
    return manifest


def fixture_process(command, env):
    """Launch the isolated runner self-test's process doubles.

    Enabled only by the nested runner self-test's explicit CLI flag. Copies
    must live within its isolated root; ordinary commands keep their real ABI.
    """
    command = [str(x) for x in command]
    if FIXTURE_EXECUTABLE is None:
        return command, env
    executable = Path(command[0]).resolve()
    if not executable.is_relative_to(ROOT) or not executable.is_file():
        return command, env
    # Only published mock executables are redirected. Python, CMake and other
    # host tools (including test scripts under ROOT) are never intercepted.
    if hashlib.sha256(executable.read_bytes()).digest() != hashlib.sha256(FIXTURE_EXECUTABLE.read_bytes()).digest():
        return command, env
    source = ROOT / "scripts/quality/regression_runner_mock.py"
    if env.get("VPP_TEST_FAKE") != "1" or not source.is_file():
        raise RuntimeError("invalid runner JIT fixture context")
    return [sys.executable, str(source), str(executable), *command[1:]], env


def execute(command, cwd, env, logs, name, timeout, *, stdin=None):
    start = time.monotonic()
    command, env = fixture_process(command, env)
    with (logs / (name + ".stdout")).open("wb") as out, (logs / (name + ".stderr")).open("wb") as err, (stdin.open("rb") if stdin is not None else contextlib.nullcontext(subprocess.DEVNULL)) as input_stream:
        process = subprocess.Popen([str(x) for x in command], cwd=cwd, env=env,
                                   stdin=input_stream, stdout=out, stderr=err,
                                   start_new_session=(os.name != "nt"))
        try:
            status = process.wait(timeout=timeout)
        except BaseException:
            if process.poll() is None:
                if os.name == "nt":
                    process.kill()
                else:
                    os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            raise
        finally:
            (logs / (name + ".seconds")).write_text(f"{time.monotonic() - start:.2f}\n")
    (logs / (name + ".status")).write_text(str(status))
    return status


def build(compiler, source, binary, cwd, env, logs, timeout):
    return execute([compiler, "build", source, "-o", binary], cwd, env, logs, "build", timeout)


def batch_eligible(case):
    # Only ordinary positive builds with identical compiler environments.
    # Keep compiler rejection, custom environments and harnesses independent.
    return case["suite"] == "full" and not any(key in case for key in (
        "dump", "linker_phase", "vm_phase", "cmake", "compile_reject",
        "diagnostic", "env", "timezone", "dns_mock", "tooling_test"))


def build_batch(cases, compiler, directories, logs, env, timeout, group):
    """Return verified publications and a failure reason, if any."""
    batch_logs = logs / "batches" / str(group)
    batch_logs.mkdir(parents=True)
    batch_env = dict(env, VPP_NATIVE_NO_CACHE="0",
                     VPP_PREFERENCES_HOME=str(batch_logs / "preferences"))
    if batch_env.get("VPP_COMPILER_PROFILE_FILE"):
        batch_env["VPP_COMPILER_PROFILE_FILE"] = str(batch_logs / "compiler.profile")
    command = [compiler, "batch-build"]
    for case in cases:
        command.extend((ROOT / case["source"], directories[case["id"]] / executable_name("program")))
    failure = ""
    try:
        status = execute(command, ROOT, batch_env, batch_logs, "batch", timeout * len(cases))
        if status:
            failure = f"batch exit {status}"
    except (OSError, subprocess.TimeoutExpired) as exc:
        failure = str(exc)
        (batch_logs / "batch.error").write_text(failure, encoding="utf-8")
    completed = {}
    stdout = batch_logs / "batch.stdout"
    if stdout.is_file():
        for line in stdout.read_text(encoding="utf-8", errors="replace").splitlines():
            if not line.startswith("VPP_BATCH_BUILD "):
                continue
            match = re.fullmatch(r"VPP_BATCH_BUILD (\d+) (\S+)", line)
            try:
                index, milliseconds = int(match[1]), float(match[2])
            except (TypeError, ValueError):
                failure = failure or "invalid batch completion marker"
                break
            if (index != 2 * len(completed) + 1 or index >= 2 * len(cases)
                    or not math.isfinite(milliseconds) or milliseconds < 0):
                failure = failure or "invalid batch completion order or duration"
                break
            name = cases[(index - 1) // 2]["id"]
            if not (directories[name] / executable_name("program")).is_file():
                failure = failure or "batch did not publish executable"
                break
            completed[name] = milliseconds / 1000
    if len(completed) != len(cases):
        failure = failure or "missing batch completion markers"
    # A nonzero exit after all publications still fails the final case.
    if failure and len(completed) == len(cases):
        completed.pop(cases[-1]["id"])
    for case in cases:
        name = case["id"]
        directory = logs / name
        directory.mkdir(exist_ok=True)
        (directory / "build.batch").write_text(f"batches/{group}\n")
        if name in completed:
            (directory / "build.seconds").write_text(f"{completed[name]:.2f}\n")
            (directory / "build.status").write_text("0")
        else:
            # Do not run or retry an unverified publication: that could mask
            # a compiler session failure.
            (directories[name] / executable_name("program")).unlink(missing_ok=True)
            (directory / "build.status").write_text("1")
            for suffix in ("stdout", "stderr", "error"):
                source = batch_logs / f"batch.{suffix}"
                if source.is_file():
                    shutil.copyfile(source, directory / f"build.{suffix}")
    return completed, failure


def prepare_vm(compiler, logs, env, timeout):
    # CI packages one VM per platform. Local runs build it once.
    packaged = compiler.with_name(executable_name("vpp-vm"))
    if packaged.is_file():
        print(f"SETUP shared VM: {packaged}", flush=True)
        return packaged
    directory = logs / "shared-vm"
    directory.mkdir(parents=True, exist_ok=True)
    vm = directory / executable_name("vpp-vm")
    vm_env = dict(env)
    if platform.system() == "Linux":
        vm_env["VPP_NATIVE_ELF_LINK"] = "1"
    print("SETUP building shared VM", flush=True)
    i32 = compiler.with_name(executable_name("vpp-i32"))
    source = ROOT / "gói/máy ảo/chính.vi"
    if platform.system() == "Windows":
        from check_windows_full_bootstrap import raw_build
        raw_build(i32, source, vm, vm_env, directory, vm_core=True, timeout=timeout)
        status = 0
    elif platform.system() == "Darwin" and platform.machine() == "arm64":
        status = execute(["/bin/bash", ROOT / "scripts/macos/build-vpp-vm.sh", i32, vm],
                         ROOT, vm_env, directory, "build", timeout)
    else:
        status = build(i32, source, vm, ROOT, vm_env, directory, timeout)
    if status or not vm.is_file():
        raise RuntimeError(f"Shared VM build failed (exit {status}); logs: {directory}")
    return vm


def verify_failure(case, status, logs, stage):
    if status != case["exit"]:
        raise RuntimeError(f"{stage} exit {status}, expected {case['exit']}")
    combined = (logs / (stage + ".stdout")).read_bytes() + (logs / (stage + ".stderr")).read_bytes()
    if case["diagnostic"].encode("utf-8") not in combined:
        raise RuntimeError(f"{stage} missing diagnostic: {case['diagnostic']}")


def jit_dump(case, jit, source, cwd, env, logs, timeout):
    # Execute the same compiler library functions used by the native CLI.
    common = "nhập gói.nhập xuất.tệp.đọc tệp;\nnhập gói.hệ thống.môi trường.đọc biến môi trường;\n"
    if case["dump"] == "disassemble":
        imports = ("nhập gói.biên dịch.nguồn tệp.biên dịch nguồn tệp tự chủ;\n"
                   "nhập gói.biên dịch.bộ nạp.biên dịch nạp sản phẩm biên dịch;\n"
                   "nhập gói.biên dịch.giải mã bytecode.biên dịch giải mã chương trình;\n")
        expression = 'biên dịch giải mã chương trình(biên dịch nạp sản phẩm biên dịch(biên dịch nguồn tệp tự chủ(đường, đọc biến môi trường("VPP_HOME", ""))))'
    else:
        function = {"dump-ast": "biên dịch hiển thị cây cú pháp", "dump-ir": "biên dịch hiển thị biểu diễn trung gian"}[case["dump"]]
        imports = "nhập gói.biên dịch.hiển thị cây và IR." + function + ";\n"
        expression = function + "(đọc tệp(đường))"
    harness = cwd / "dump.vi"
    harness.write_text(common + imports + 'hàm chính() { đường = đọc biến môi trường("VPP_TEST_SOURCE", ""); in ' + expression + '; };\n', encoding="utf-8")
    env = dict(jit_environment(env), VPP_TEST_SOURCE=str(source))
    for name in ("first", "second"):
        if execute([jit, "--dynamic", harness], cwd, env, logs, name, timeout):
            raise RuntimeError(f"JIT {case['dump']} failed")
        if (logs / (name + ".stderr")).read_bytes():
            raise RuntimeError(f"JIT {case['dump']} emitted stderr")
    first = (logs / "first.stdout").read_bytes()
    if not first.strip() or first != (logs / "second.stdout").read_bytes():
        raise RuntimeError(f"{case['dump']} produced empty or unstable output")


def jit_i32_print(jit, source, cwd, env, logs, timeout):
    env = jit_environment(env)
    def check(path, label, status, stdout=None, diagnostic=None):
        actual = execute([jit, path], cwd, env, logs, label, timeout)
        out = (logs / (label + ".stdout")).read_bytes()
        err = (logs / (label + ".stderr")).read_bytes()
        if actual != status or (stdout is not None and normalize(out) != normalize(stdout)) or (diagnostic and diagnostic.encode() not in out + err):
            raise RuntimeError(f"JIT i32 print {label}: unexpected exit/output ({actual})")
        if diagnostic is None and err:
            raise RuntimeError(f"JIT i32 print {label}: unexpected stderr")
    check(source, "run", 7, b"-2147483648\n42\n0\n")
    for label, code, status, stdout, diagnostic in (
        ("print-variable", "hàm chính() { biến số = 42; in số; trả về 0; };", 0, b"42\n", None),
        ("string-print", "hàm chính() { in ; trả về 0; };", 1, None, "thiếu toán hạng"),
        ("legacy-print", "hàm chính() { in số(7); trả về 0; };", 1, None, "hàm chưa định nghĩa"),
        ("reserved-symbol", "hàm vpp_native_print_i32(x) { trả về x; }; hàm main() { trả về 0; };", 1, None, "dành riêng"),
    ):
        probe = cwd / (label + ".vi")
        probe.write_text(code + "\n", encoding="utf-8")
        check(probe, label, status, stdout, diagnostic)


def jit_i32_io(case, jit, source, cwd, env, logs, timeout):
    """Preserve the independent byte/file/stdin oracles of the native harnesses."""
    env = jit_environment(env)
    kind = case["cmake"]

    def check(path, label, stdout=None, diagnostic=None, stdin=None):
        # Standalone i32 literals and byte/file handles need a fresh arena,
        # rather than the compiler's ordinary function-call arena.
        status = execute([jit, "--raw-program", path], cwd, env, logs, label, timeout, stdin=stdin)
        out = (logs / (label + ".stdout")).read_bytes()
        err = (logs / (label + ".stderr")).read_bytes()
        if diagnostic is not None:
            if status != 1 or diagnostic.encode() not in out + err:
                raise RuntimeError(f"{label}: expected JIT rejection {diagnostic!r}, got {status}")
        elif status or normalize(out) != normalize(stdout) or err:
            raise RuntimeError(f"{label}: unexpected JIT output/exit {status}")

    if kind == "checknativebulk.cmake":
        check(source, "run", "4096\n4096\n1820\n0\nNative UTF-8: Việt Nam\n".encode())
        payload = (cwd / "native-block.bin").read_bytes()
        if len(payload) != 10012 or hashlib.sha256(payload).hexdigest() != "4a3fb371a00b860295a5096738c28d7dbedd444d52277cc992187b950a0e1047":
            raise RuntimeError("native bulk file size/hash mismatch")
        arity = "tệp đọc khối(1)"
        reserved = ("byte nối", "byte bằng", "byte in", "tệp đọc khối", "tệp ghi khối",
                    "vpp_native_bytes_concat", "vpp_native_file_read_block")
    elif kind == "checknativebytefile.cmake":
        check(source, "run", b"4\n330\n-1\n-2\n")
        for name in ("vpp-io.in", "vpp-io.out"):
            if (cwd / name).read_bytes() != bytes.fromhex("00ff0a41"):
                raise RuntimeError(f"{name}: incorrect binary bytes")
        arity = "byte thêm(1)"
        reserved = ("byte mới", "tệp đóng", "vpp_native_bytes_get", "vpp_native_file_close")
    elif kind == "checknativebytesops.cmake":
        check(source, "run", b"2097152\n90\n")
        if (cwd / "native-slice-cap.bin").read_bytes() != b"A":
            raise RuntimeError("native slice boundary file mismatch")
        arity = "byte cắt(1, 0)"
        reserved = ("byte cắt", "byte tìm", "byte làm rỗng", "vpp_native_bytes_slice",
                    "vpp_native_bytes_find", "vpp_native_bytes_clear")
    elif kind == "checknativeread.cmake":
        for label, data, expected in (
            ("boundary", b"-2147483648\n2147483647\n", b"-2147483648\n0\n2147483647\n0\n"),
            ("invalid", b"12abc\n-1\n", b"0\n2\n-1\n0\n"),
            ("overflow", b"2147483648\n0\n", b"0\n3\n0\n0\n"),
            ("eof", b"", b"0\n1\n0\n1\n"),
            ("longline", b"9" * 80 + b"\n17\n", b"0\n2\n17\n0\n"),
            ("whitespace", b"  +42  \n-0\n", b"42\n0\n0\n0\n"),
        ):
            input_file = cwd / (label + ".txt")
            input_file.write_bytes(data)
            check(source, label, expected, stdin=input_file)
        arity = "đọc số(12)"
        reserved = ("đọc số", "vpp_native_read_i32", "vpp_native_read_status")
    else:
        raise ValueError(kind)
    probe = cwd / "bad-arity.vi"
    probe.write_text(f"hàm chính() {{ trả về {arity}; }};\n", encoding="utf-8")
    check(probe, "bad-arity", diagnostic="không nhận đối số" if kind == "checknativeread.cmake" else "sai số đối số")
    for i, name in enumerate(reserved):
        probe = cwd / f"reserved-{i}.vi"
        probe.write_text(f"hàm {name}() {{ trả về 0; }}; hàm chính() {{ trả về 0; }};\n", encoding="utf-8")
        check(probe, f"reserved-{i}", diagnostic="dành riêng")


def jit_i32_frontend(case, jit, source, cwd, env, logs, timeout):
    """Run the original byte-handle lexer/parser harness without an executable."""
    env = jit_environment(env)
    native = ROOT / "gói/biên dịch/bản địa"
    parts = [native / "phân tích từ vựng.vi"]
    parser = case["cmake"] == "checknativeparser.cmake"
    if parser:
        parts.extend(native / name for name in ("từ tố.vi", "phân tích cú pháp.vi", "biểu diễn trung gian.vi"))
        (cwd / "native-parser-input.vi").write_bytes("// ví dụ\r\nhàm chính() { biến a = 12; trả về a * 4 + 4; };\n".encode())
    else:
        (cwd / "native-lexer-input.vi").write_bytes('// header\r\nhàm mẫu(a) { /* chú */ biến x = 12; in "Chào\\n"; trả về x; };\r\n'.encode())
    parts.append(source)
    combined = cwd / "frontend.vi"
    combined.write_text("\n".join(re.sub(r"(?m)^[ \t]*nhập[^\n]*;[ \t]*$", "", part.read_text(encoding="utf-8")) for part in parts), encoding="utf-8")
    status = execute([jit, "--raw-program", combined], cwd, env, logs, "run", timeout)
    if status or normalize((logs / "run.stdout").read_bytes()) != b"20261017\n" or (logs / "run.stderr").read_bytes():
        raise RuntimeError(f"native JIT frontend failed: {status}")
    if not parser:
        return
    expected = bytes.fromhex("010000000c0000000300000000000000020000000000000001000000040000000600000000000000010000000400000004000000000000000700000000000000")
    if (cwd / "native-parser-output.ir").read_bytes() != expected:
        raise RuntimeError("native parser IR differs from independent wire oracle")
    reserved = ("byte đẩy số nguyên ba mươi hai bit", "byte đọc số nguyên ba mươi hai bit",
                "byte ghi số nguyên ba mươi hai bit", "vpp_native_bytes_push_i32",
                "vpp_native_bytes_load_i32", "vpp_native_bytes_store_i32", "máy chia số nguyên ba mươi hai bit")
    probes = [(f"reserved-{i}", f"hàm {name}() {{ trả về 0; }}; hàm chính() {{ trả về 0; }};", "dành riêng") for i, name in enumerate(reserved)]
    probes.extend((label, f"hàm chính() {{ trả về {call}; }};", "sai số đối số") for label, call in (
        ("bad-parser-arity", "byte ghi số nguyên ba mươi hai bit(1, 2)"),
        ("bad-native-div-arity", "máy chia số nguyên ba mươi hai bit(52)")))
    for label, code, diagnostic in probes:
        path = cwd / (label + ".vi")
        path.write_text(code + "\n", encoding="utf-8")
        status = execute([jit, "--raw-program", path], cwd, env, logs, label, timeout)
        if status != 1 or diagnostic.encode() not in (logs / (label + ".stdout")).read_bytes() + (logs / (label + ".stderr")).read_bytes():
            raise RuntimeError(f"{label}: native JIT parser did not reject with {diagnostic}")


def jit_i32_codegen(jit, source, cwd, env, logs, timeout):
    """Keep the raw codegen oracle and execute the exact emitted Mach-O object."""
    from check_native_macho_layout import parse, catalog, verify_native_signature
    env = jit_environment(env)
    native = ROOT / "gói/biên dịch/bản địa"
    parts = [native / name for name in ("phân tích từ vựng.vi", "từ tố.vi", "phân tích cú pháp.vi",
             "biểu diễn trung gian.vi", "sinh mã.vi", "đối tượng.vi")]
    (cwd / "native-codegen-input.vi").write_text(
        "hàm chính() { biến x = -159; x /= -3; x %= 52; x += 51; x &= 127; x |= 128; x ^= 128; x <<= 1; x >>= 1; x = ~~x; biến p = 1|2&4; x += p-1; nếu (!sai) { trả về x; }; trả về 1; };\n",
        encoding="utf-8")
    combined = cwd / "codegen.vi"
    combined.write_text("\n".join(re.sub(r"(?m)^[ \t]*nhập[^\n]*;[ \t]*$", "", part.read_text(encoding="utf-8"))
                                  for part in [*parts, source]), encoding="utf-8")
    status = execute([jit, "--raw-program", combined], cwd, env, logs, "run", timeout)
    if status or normalize((logs / "run.stdout").read_bytes()) != b"20261017\n" or (logs / "run.stderr").read_bytes():
        raise RuntimeError(f"native JIT codegen failed: {status}")
    obj = cwd / "native-codegen-output.o"
    sections, symbols = parse(obj)
    if len(sections) != 1 or sections[0][:2] != (0, 0) or sections[0][2] <= 0 or sections[0][4] or symbols != [("_main", 15, 0, 0)]:
        raise RuntimeError("unexpected native codegen Mach-O layout")
    # The executable is an output of the linker feature under test, not a
    # compiled test harness. No external assembler or linker participates.
    result = cwd / "native-codegen-result"
    status = execute([jit, "--dynamic", TESTS / "fixtures/native_macho_executable.vi", "|".join(catalog()), result, obj],
                     cwd, env, logs, "link-oracle", max(timeout, 180))
    if status or normalize((logs / "link-oracle.stdout").read_bytes()) != b"Mach-O executable: PASS\n" or (logs / "link-oracle.stderr").read_bytes():
        raise RuntimeError(f"V++ JIT Mach-O link oracle failed: {status}")
    raw = result.read_bytes()
    if struct.unpack_from("<4I", raw) != (0xfeedfacf, 0x100000c, 0, 2):
        raise RuntimeError("invalid executable Mach-O header")
    verify_native_signature(raw)
    result.chmod(0o700)
    if execute(["/usr/bin/codesign", "--verify", result], cwd, env, logs, "signature", 30):
        raise RuntimeError("native codegen executable signature invalid")
    status = execute([result], cwd, env, logs, "machine-code", 30)
    if status != 52 or (logs / "machine-code.stdout").read_bytes() or (logs / "machine-code.stderr").read_bytes():
        raise RuntimeError(f"native codegen machine code returned {status}, expected 52")


def run_case(case, compiler, logs, env, timeout, vm=None, *, phase="both", work=None,
             prepared_binary=None, selector=None, jit=None):
    # Reject a toolchain case before entering any build/harness branch.
    if jit is not None and (phase == "build" or selector is not None or toolchain_reason(case, jit_mode=True)):
        raise RuntimeError("invalid JIT execution plan")
    logs.mkdir(parents=True, exist_ok=True)
    env = dict(env)
    env.update(case.get("env", {}))
    if env.get("VPP_COMPILER_PROFILE_FILE"):
        env["VPP_COMPILER_PROFILE_FILE"] = str(logs / "compiler.profile")
    if "timezone" in case:
        env["TZ"] = case["timezone"]
    with (tempfile.TemporaryDirectory(prefix="vpp-regression-") if work is None
          else contextlib.nullcontext(work)) as work:
        cwd = Path(work)
        if case.get("hosts_fixture"):
            hosts = cwd / "socket.hosts"
            hosts.write_text("203.0.113.21 fixture.test alias.test\n::1 fixture.test\n"
                             "0:0:0:0:0:0:0:1 fixture.test\n999.1.1.1 fixture.test\n", encoding="ascii")
            env.update(VPP_DNS_VI_HOSTS_FILE=str(hosts), VPP_SOCKET_VI_ONLY="0",
                       VPP_DNS_VI_SERVER="", VPP_DNS_VI_SERVERS="", VPP_DNS_VI_RESOLV_CONF="")
        env["VPP_PREFERENCES_HOME"] = str(cwd / "preferences")
        binary = prepared_binary if prepared_binary is not None else cwd / executable_name("program")
        if case.get("tooling_test"):
            test_work = cwd / "tooling-test-work"
            test_work.mkdir(exist_ok=True)
            env.update(
                VPP_TEST_ROOT=str(ROOT),
                VPP_TEST_WORK=str(test_work),
                VPP_TEST_PYTHON=sys.executable,
                VPP_TEST_CMAKE=shutil.which("cmake") or "cmake",
                VPP_TEST_SELF=str(binary),
            )
            if jit is not None and case["id"] == "kiem_tra_regression_runner":
                env.update(VPP_TEST_SELF=str(jit), VPP_TEST_JIT=str(jit))
        source = ROOT / case["source"]
        if phase == "build" and any(key in case for key in HARNESS_KEYS):
            return True  # These harnesses exercise compilation as part of the test.
        if jit is not None and "dump" in case:
            return jit_dump(case, jit, source, cwd, env, logs, timeout)
        if jit is not None and case.get("cmake") == "checknativeprint.cmake":
            return jit_i32_print(jit, source, cwd, env, logs, timeout)
        if jit is not None and case.get("cmake") in {"checknativelexer.cmake", "checknativeparser.cmake"}:
            return jit_i32_frontend(case, jit, source, cwd, env, logs, timeout)
        if jit is not None and case.get("cmake") == "checknativecodegen.cmake":
            return jit_i32_codegen(jit, source, cwd, env, logs, timeout)
        if jit is not None and case.get("cmake") in JIT_I32_HARNESSES:
            return jit_i32_io(case, jit, source, cwd, env, logs, timeout)
        if "dump" in case:
            for name in ("first", "second"):
                if execute([compiler, case["dump"], source], cwd, env, logs, name, timeout):
                    raise RuntimeError(f"{case['dump']} failed")
            first = (logs / "first.stdout").read_bytes()
            if not first.strip() or first != (logs / "second.stdout").read_bytes():
                raise RuntimeError(f"{case['dump']} produced empty or unstable output")
            return
        if "linker_phase" in case:
            command = [sys.executable, ROOT / "scripts/quality/check_native_regression_linkers.py"]
            if jit is not None:
                command += ["--jit", case["linker_phase"], jit, cwd]
                env = jit_environment(env)
            else:
                command += [case["linker_phase"], compiler, cwd]
            status = execute(command, cwd, env, logs, "harness",
                             max(timeout, case.get("harness_timeout", 3600)))
            if status:
                raise RuntimeError(f"Linker {case['linker_phase']} harness exit {status}")
            return
        if "vm_phase" in case:
            cmake = shutil.which("cmake")
            if not cmake:
                raise RuntimeError("CMake is required for the VM oracle")
            cmd = [cmake, f"-DVPP_ROOT={ROOT}", f"-DVPP_OUTPUT_DIR={cwd}",
                   f"-DVPP_VM_PHASE={case['vm_phase']}"]
            if jit is not None:
                vm = vm or jit.with_name(executable_name("vpp-vm"))
                if not vm.is_file():
                    raise RuntimeError(f"packaged standalone VM missing: {vm}")
                cmd += [f"-DVPP_JIT={jit}", f"-DVPP_PYTHON={sys.executable}"]
                nm = shutil.which("llvm-nm") or shutil.which("nm")
                if nm:
                    cmd.append(f"-DVPP_VM_NM={nm}")
                env = jit_environment(env)
            else:
                cmd += [f"-DVPP_STAGE1={compiler.with_name(executable_name('vpp-i32'))}",
                        f"-DVPP_NATIVE_COMPILER={compiler}"]
            if vm is not None:
                # DNS provider discovery uses the VM's directory. Keep each
                # phase's generated providers beside its own copy of the VM.
                local_vm = cwd / executable_name("vpp-vm")
                shutil.copy2(vm, local_vm)
                cmd.append(f"-DVPP_VM={local_vm}")
            cmd += ["-P", ROOT / "scripts/quality/check_vm_asm.cmake"]
            try:
                status = execute(cmd, cwd, env, logs, "harness", max(timeout, 3600))
            finally:
                for path in cwd.rglob("*"):
                    if path.is_file() and path.suffix in {".out", ".err", ".log", ".stdout", ".stderr", ".status"}:
                        destination = logs / "vm-diagnostics" / path.relative_to(cwd)
                        destination.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(path, destination)
            if status:
                raise RuntimeError(f"Standalone VM {case['vm_phase']} harness exit {status}")
            return
        if jit is not None and case.get("cmake") == "check_native_runtime.cmake":
            cmake = shutil.which("cmake")
            if not cmake:
                raise RuntimeError("CMake is required for the native runtime oracle")
            cmd = [cmake, f"-DVPP_JIT={jit}", f"-DVPP_ROOT={ROOT}",
                   f"-DVPP_TARGET={env.get('VPP_NATIVE_TARGET', 'native')}",
                   f"-DVPP_OUTPUT_DIR={cwd}", "-P", ROOT / "scripts/quality/check_native_runtime.cmake"]
            if execute(cmd, cwd, jit_environment(env), logs, "harness", timeout):
                raise RuntimeError("JIT native runtime harness failed")
            return
        if "cmake" in case:
            cmd = ["cmake", f"-DVPP_EXEC={compiler}", f"-DVPP_STAGE1={compiler}",
                   f"-DVPP_ROOT={ROOT}", f"-DVPP_HOME={env['VPP_HOME']}",
                   f"-DVPP_SOURCE={source}", f"-DVPP_HARNESS={source}",
                   f"-DVPP_KERNEL={ROOT / 'gói/biên dịch/bản địa/phân tích từ vựng.vi'}",
                   f"-DVPP_TARGET={env.get('VPP_NATIVE_TARGET', 'native')}",
                   f"-DVPP_OUTPUT_DIR={cwd}", "-P", ROOT / "scripts/quality" / case["cmake"]]
            status = execute(cmd, cwd, env, logs, "harness", timeout)
            if status:
                raise RuntimeError(f"CMake harness exit {status}")
            return
        if jit is not None:
            original_path = env.get("PATH", "")
            env = jit_environment(env)
            if case["id"] in {"kiem_tra_do_thi_build", "kiem_tra_regression_runner"}:
                # The graph fixture invokes fake shell scripts, never a real compiler.
                env["PATH"] = original_path
            if case.get("compile_reject") or case.get("failure_phase") == "build":
                status = execute([jit, "--check-dynamic", source], cwd, env, logs, "build", timeout)
                if binary.exists():
                    raise RuntimeError("JIT check produced a guest executable")
                if case.get("compile_reject"):
                    if status != 1 or "V++ JIT: từ chối cú pháp".encode() not in (logs / "build.stdout").read_bytes():
                        raise RuntimeError(f"expected JIT syntax rejection, got exit {status}")
                else:
                    verify_failure(case, status, logs, "build")
                return False
        elif phase != "run":
            status = build(compiler, source, binary, ROOT, env, logs, timeout)
            if case.get("compile_reject"):
                if status != 1 or binary.exists():
                    raise RuntimeError(f"malformed source: expected build exit 1 and no executable; got {status}")
                return False
            if status:
                if "diagnostic" in case and case.get("failure_phase") != "run":
                    verify_failure(case, status, logs, "build")
                    return False
                raise RuntimeError(f"build exit {status}")
            if not binary.is_file():
                raise RuntimeError("compiler did not produce executable")
            if case.get("dns_mock"):
                prepare_dns_mock(compiler, cwd, env, logs, timeout)
            if phase == "build":
                return True
        if jit is None and not binary.is_file():
            raise RuntimeError("missing prepared executable")
        if case["suite"] == "full" and not (jit is not None and fixture_free(case, source)):
            # Test programs may write relative paths; do not mutate the checkout.
            shutil.copytree(TESTS, cwd / "scripts/test", ignore=shutil.ignore_patterns(".*"))
            shutil.copytree(ROOT / "gói", cwd / "gói")
            shutil.copytree(TESTS / "fixtures", cwd / "fixtures", ignore=shutil.ignore_patterns(".*"))
            if (TESTS / "expected").is_dir():
                shutil.copytree(TESTS / "expected", cwd / "expected")
        if case.get("dns_mock"):
            if jit is not None:
                run_dns_mock([jit, "--dynamic", source], cwd, env, logs,
                             server=[jit, "--raw-program", TESTS / "fixtures/native_dns_mock.vi"],
                             timeout=timeout)
            else:
                run_dns_mock(binary, cwd, env, logs)
            return
        args = case.get("args", []) if selector is None else [selector]
        if jit is not None and case["id"] == "i32-logical-side-effects":
            # The i32 JIT accepts assignment expressions; it has no module loader.
            # Flatten only the declared, function-only local module for this case.
            module, = case["sources"]
            text = source.read_text(encoding="utf-8")
            directive = "nhập native_logical_side_effects.*;"
            if text.count(directive) != 1:
                raise RuntimeError("unexpected i32 fixture import")
            combined = cwd / "logical-side-effects.vi"
            combined.write_text((ROOT / module).read_text(encoding="utf-8") + "\n" + text.replace(directive, "", 1), encoding="utf-8")
            command = [jit, combined]
        elif jit is not None and case["id"] == "i32-logical-call-side-effects":
            # This oracle checks the i32 frontend's short-circuit lowering.
            # The dynamic bytecode frontend has eager logical operators.
            command = [jit, "--raw-program", source, *args]
        else:
            command = [jit, "--dynamic", source, *args] if jit is not None else [binary, *args]
        status = execute(command, cwd, env, logs, "run", timeout)
        if jit is not None:
            if binary.exists():
                raise RuntimeError("JIT produced a guest executable")
            if not (case.get("run_diagnostic_regex") or case.get("diagnostic") or "expected_stderr" in case) and (logs / "run.stderr").read_bytes():
                raise RuntimeError("unexpected JIT stderr")
        if "diagnostic" in case:
            if case.get("failure_phase") == "build":
                raise RuntimeError("expected build rejection, but compiler produced an executable")
            verify_failure(case, status, logs, "run")
            return
        if status != case.get("exit", 0):
            raise RuntimeError(f"run exit {status}, expected {case.get('exit', 0)}")
        if "expected_stderr" in case:
            expected_stderr = (ROOT / case["expected_stderr"]).read_bytes()
            actual_stderr = (logs / "run.stderr").read_bytes()
            if normalize_stderr(actual_stderr) != normalize_stderr(expected_stderr):
                (logs / "expected.stderr").write_bytes(expected_stderr)
                raise RuntimeError("stderr differs from golden")
        if "run_diagnostic_regex" in case:
            stderr = (logs / "run.stderr").read_text(encoding="utf-8", errors="replace")
            if not re.search(case["run_diagnostic_regex"], stderr):
                raise RuntimeError("missing expected runtime diagnostic")
        if "run_forbidden_regex" in case:
            stderr = (logs / "run.stderr").read_text(encoding="utf-8", errors="replace")
            if re.search(case["run_forbidden_regex"], stderr):
                raise RuntimeError("unexpected runtime diagnostic")
        for name in case.get("absent", []):
            if (cwd / name).exists():
                raise RuntimeError(f"test left unexpected file: {name}")
        expected = (ROOT / case["expected"]).read_bytes() if "expected" in case else case["stdout"].encode()
        actual = (logs / "run.stdout").read_bytes()
        if normalize(actual) != normalize(expected):
            (logs / "expected.stdout").write_bytes(expected)
            raise RuntimeError("stdout differs from golden")


def run_dns_mode(server, client, cwd, env, logs, mode, queries, image=None, timeout=30, batch_client=None):
    # Keep the actual V++ server and client on every OS; Python only manages
    # their lifetime and checks the protocol between the test processes.
    gated = image is None and isinstance(client, list) and "--dynamic" in client
    ready_file = logs / "client.ready"
    ready_file.unlink(missing_ok=True)
    config = logs / "config"
    config.write_bytes(f"{mode}\n{-queries if gated else queries}\n".encode("ascii"))
    hosts = logs / "empty-hosts"
    hosts.write_bytes(b"")
    server_out = logs / "server.out"
    deadline = time.monotonic() + timeout
    with config.open("rb") as inp, server_out.open("wb") as out, (logs / "server.err").open("wb") as err:
        worker = subprocess.Popen([str(x) for x in server] if isinstance(server, list) else [str(server)], cwd=cwd, env={**env, "PATH": ""},
                                  stdin=subprocess.PIPE if gated else inp, stdout=out, stderr=err)
        try:
            if gated:
                worker.stdin.write(config.read_bytes())
                worker.stdin.flush()
            while True:
                data = server_out.read_bytes()
                if b"\n" in data:
                    port = data.splitlines()[0].strip()
                    if not port.isdigit() or not 0 < int(port) <= 65535:
                        raise RuntimeError("DNS server returned invalid port")
                    break
                if worker.poll() is not None:
                    raise RuntimeError(f"DNS server exited before readiness: {worker.returncode}")
                if time.monotonic() >= deadline:
                    raise RuntimeError("DNS server readiness timed out")
                time.sleep(0.02)
            client_env = dict(env, VPP_DNS_MOCK_BATCH="0", VPP_DNS_MOCK_MODE=str(mode), VPP_DNS_VI_PORT=port.decode("ascii"),
                              VPP_DNS_VI_SERVER="127.0.0.1", VPP_DNS_VI_SERVERS="127.0.0.2,127.0.0.1" if mode == 4 else "",
                              VPP_DNS_VI_HOSTS_FILE=str(hosts), VPP_DNS_VI_RESOLV_CONF="", VPP_SOCKET_VI_ONLY="1")
            if image is not None:
                client_env["PATH"] = ""
            command = (client if isinstance(client, list) else [client]) if image is None else [client, image]
            if batch_client is not None:
                worker.stdin.write(b"1\n")
                worker.stdin.flush()
                status = batch_client(mode, int(port), logs, deadline)
            elif gated:
                client_env["VPP_DNS_MOCK_READY"] = str(ready_file)
                # Compilation remains bounded by the original harness deadline.
                # The fixture's five-second receive timer starts only when the
                # actual guest is ready. UDP can queue while the gate is released.
                with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
                    task = pool.submit(execute, command, cwd, client_env, logs, "client",
                                       max(0.1, deadline - time.monotonic()))
                    while not ready_file.exists() and not task.done() and time.monotonic() < deadline:
                        time.sleep(0.01)
                    if ready_file.exists():
                        worker.stdin.write(b"1\n")
                        worker.stdin.flush()
                    status = task.result()
                    if not ready_file.exists() and status == 0:
                        raise RuntimeError("DNS client did not enter guest code")
            else:
                status = execute(command, cwd, client_env, logs, "client", max(0.1, deadline - time.monotonic()))
            if image is not None and mode >= 10:
                if status == 0 or b"error=16" not in (logs / "client.stderr").read_bytes():
                    raise RuntimeError(f"VM accepted malformed DNS or returned wrong diagnostic: {status}")
            elif status:
                raise RuntimeError(f"DNS client exit {status}")
            if worker.wait(timeout=max(0.1, deadline - time.monotonic())):
                raise RuntimeError(f"DNS server exit {worker.returncode}")
            if server_out.read_bytes().splitlines()[-1].strip() != str(queries).encode("ascii"):
                raise RuntimeError("DNS server query count mismatch")
            if image is None and normalize((logs / "client.stdout").read_bytes()) != b"VPP DNS MOCK OK\n":
                raise RuntimeError("DNS client output mismatch")
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
            details = [f"DNS mode {mode}: server exit={worker.poll()}; "
                       f"guest readiness={ready_file.exists() if gated else 'not gated'}: {exc}"]
            for name in ("client.ready", "server.out", "server.err", "client.stdout", "client.stderr"):
                path = logs / name
                if path.is_file():
                    with path.open("rb") as stream:
                        stream.seek(max(0, path.stat().st_size - 2048))
                        details.append(f"{name}: {stream.read().decode('utf-8', errors='replace')}")
            raise RuntimeError("\n".join(details)) from exc
        finally:
            if worker.poll() is None:
                worker.kill()
            worker.wait()
            if worker.stdin is not None:
                worker.stdin.close()


def prepare_dns_mock(compiler, cwd, env, logs, timeout):
    i32 = compiler.with_name(executable_name("vpp-i32"))
    i32_env = dict(env)
    if platform.system() == "Linux":
        i32_env["VPP_NATIVE_ELF_LINK"] = "1"
    server = cwd / executable_name("dns-server")
    server_logs = logs / "server-build"
    server_logs.mkdir(exist_ok=True)
    if platform.system() == "Windows":
        from check_windows_full_bootstrap import raw_build
        raw_build(i32, TESTS / "fixtures/native_dns_mock.vi", server, i32_env, server_logs, timeout=timeout)
    elif build(i32, TESTS / "fixtures/native_dns_mock.vi", server, ROOT, i32_env, server_logs, timeout):
        raise RuntimeError("DNS server build failed")
    if not server.is_file():
        raise RuntimeError("missing DNS server executable")


@contextlib.contextmanager
def dns_batch_client(client, cwd, env, logs, deadline):
    """Compile the explicit-port DNS cases once, then send one mode at a time."""
    batch_logs = logs / "client-batch"
    batch_logs.mkdir(exist_ok=True)
    hosts = batch_logs / "empty-hosts"
    hosts.write_bytes(b"")
    client_env = dict(env, VPP_DNS_MOCK_BATCH="1", VPP_DNS_MOCK_READY="",
                      VPP_DNS_VI_SERVER="127.0.0.1", VPP_DNS_VI_SERVERS="",
                      VPP_DNS_VI_HOSTS_FILE=str(hosts), VPP_DNS_VI_RESOLV_CONF="", VPP_SOCKET_VI_ONLY="1")
    command, client_env = fixture_process(client, client_env)
    stdout = batch_logs / "client.stdout"
    stderr = batch_logs / "client.stderr"
    with stdout.open("wb") as out, stderr.open("wb") as err, stdout.open("rb") as response:
        process = subprocess.Popen(command, cwd=cwd, env=client_env, stdin=subprocess.PIPE,
                                   stdout=out, stderr=err, start_new_session=(os.name != "nt"))

        def read_line(until):
            while True:
                offset = response.tell()
                line = response.readline()
                if line.endswith(b"\n"):
                    return normalize(line)
                response.seek(offset)
                if process.poll() is not None:
                    raise RuntimeError(f"DNS batch guest exited {process.returncode}: {stderr.read_bytes()[-4096:]!r}; stdout={line!r}")
                if time.monotonic() >= until:
                    raise RuntimeError("DNS batch guest timed out")
                time.sleep(0.01)

        def run_mode(mode, port, mode_logs, until):
            start = time.monotonic()
            process.stdin.write(f"{mode}\n{port}\n".encode("ascii"))
            process.stdin.flush()
            line = read_line(min(until, deadline))
            (mode_logs / "client.stdout").write_bytes(line)
            (mode_logs / "client.seconds").write_text(f"{time.monotonic() - start:.2f}")
            if line != b"VPP DNS MOCK OK\n":
                raise RuntimeError(f"DNS batch mode {mode}: unexpected output {line!r}")
            return 0

        try:
            if read_line(deadline) != b"VPP DNS MOCK READY\n":
                raise RuntimeError("DNS batch guest failed to enter main")
            yield run_mode
            process.stdin.write(b"-1\n")
            process.stdin.flush()
            if process.wait(timeout=max(0.1, deadline - time.monotonic())):
                raise RuntimeError(f"DNS batch guest exit {process.returncode}")
            if response.read() or stderr.read_bytes():
                raise RuntimeError("DNS batch guest produced unexpected trailing output")
        finally:
            if process.poll() is None:
                if os.name == "nt":
                    process.kill()
                else:
                    os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            process.stdin.close()


def run_dns_mock(client, cwd, env, logs, server=None, timeout=30):
    if server is None:
        server = cwd / executable_name("dns-server")
    modes = (0, 1, 2, 3, 4, 10, 11, 12, 13, 14, 15, 16, 17, 20, 21, 30, 31, 32, 33, 34, 35, 36, 37)
    if isinstance(client, list) and "--dynamic" in client:
        deadline = time.monotonic() + timeout
        with dns_batch_client(client, cwd, env, logs, deadline) as run_mode:
            for mode in modes:
                if mode == 3:
                    continue
                queries = 2 if mode in (0, 1, 20, 21) else 1
                mode_logs = logs / f"mode-{mode}"
                mode_logs.mkdir(exist_ok=True)
                try:
                    run_dns_mode(server, client, cwd, env, mode_logs, mode, queries,
                                 timeout=min(30, max(0.1, deadline - time.monotonic())), batch_client=run_mode)
                except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
                    raise RuntimeError(f"DNS mock mode {mode}: {exc}") from exc
        # The environment-based resolver has its own fresh guest, preserving
        # the original test of VPP_DNS_VI_PORT and the hosts-file override.
        mode_logs = logs / "mode-3"
        mode_logs.mkdir(exist_ok=True)
        run_dns_mode(server, client, cwd, env, mode_logs, 3, 2,
                     timeout=max(0.1, deadline - time.monotonic()))
        return
    failures = []
    for mode in modes:
        queries = 2 if mode in (0, 1, 3, 20, 21) else 1
        mode_logs = logs / f"mode-{mode}"
        mode_logs.mkdir(exist_ok=True)
        try:
            run_dns_mode(server, client, cwd, env, mode_logs, mode, queries, timeout=timeout)
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
            failures.append(f"mode {mode}: {exc}")
    if failures:
        raise RuntimeError("DNS mock: " + "; ".join(failures))


def prepare_http(compiler, directory, env, timeout):
    directory.mkdir(parents=True, exist_ok=True)
    binary = directory / executable_name("server")
    if build(compiler, TESTS / "http_fixture.vi", binary, ROOT, env, directory, timeout):
        raise RuntimeError("HTTP fixture failed to compile")
    return binary


def start_prepared_http(binary, directory, env, timeout=30):
    directory.mkdir(parents=True, exist_ok=True)
    command = [str(x) for x in binary] if isinstance(binary, list) else [str(binary)]
    original = command
    command, env = fixture_process(command, env)
    # Only the verified process double uses an ephemeral port. Real V++
    # HTTP clients retain the existing fixed-port protocol.
    mock_server = command != original
    port_file = directory / "server.port"
    if mock_server:
        port_file.unlink(missing_ok=True)
    log_path = directory / "server.log"
    with log_path.open("wb") as log:
        process = subprocess.Popen(command, cwd=directory, env=env, stdout=log, stderr=log)
    port = 18080
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and process.poll() is None:
            try:
                if mock_server:
                    port = int(port_file.read_text(encoding="ascii"))
                    if not 0 < port <= 65535:
                        raise ValueError("invalid fixture port")
                with opener.open(f"http://127.0.0.1:{port}/health", timeout=1) as response:
                    if json.loads(response.read()) == {"ok": True} and process.poll() is None:
                        return process
            except (OSError, ValueError):
                time.sleep(0.2)
        detail = log_path.read_bytes()[-4096:].decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"HTTP fixture not ready on localhost:{port}; exit={process.poll()}; {detail}")
    except BaseException:
        stop_http(process)
        raise


def start_http(compiler, directory, env, timeout):
    return start_prepared_http(prepare_http(compiler, directory, env, timeout), directory, env)


def stop_http(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def staged_regression(cases, compiler, logs, env, timeout, jobs, record, batch_size=1,
                      module_suites=False, artifacts=None):
    """Build each fixture once, then execute prepared binaries in parallel.

    Compiler/linker/VM harnesses intentionally compile their own inputs as
    part of their assertions. The ordinary AOT run phase never calls build.
    """
    errors = (OSError, RuntimeError, AssertionError, struct.error, subprocess.TimeoutExpired)
    vm = None
    http_binary = None
    setup_errors = {}
    http_dir = (artifacts if artifacts is not None else logs) / "http-fixture"
    if any("vm_phase" in c for c in cases):
        try:
            vm = prepare_vm(compiler, logs, env, timeout)
        except errors as exc:
            setup_errors["vm_phase"] = str(exc)
    if any(c.get("http") for c in cases):
        try:
            http_binary = prepare_http(compiler, http_dir, env, timeout)
        except errors as exc:
            setup_errors["http"] = str(exc)
    elapsed = {}
    http_lock = threading.Lock()
    # A persistent build artifact can be extracted elsewhere and run repeatedly.
    # Normal combined runs keep executables off the diagnostics artifact.
    with (tempfile.TemporaryDirectory(prefix="vpp-regression-build-") if artifacts is None
          else contextlib.nullcontext(artifacts / "work")) as work:
        work = Path(work)
        work.mkdir(parents=True, exist_ok=True)
        directories = {c["id"]: work / str(i) for i, c in enumerate(cases)}
        for directory in directories.values():
            directory.mkdir()
        suites = plan_module_suites([c for c in cases if not any(k in c for k in setup_errors)]) if module_suites else {}
        prepared = {}
        for name, members in suites.items():
            binary = work / executable_name("module-" + name)
            for index, (case, _) in enumerate(members):
                prepared[case["id"]] = (binary, str(index))

        def stage(case, phase):
            name = case["id"]
            start = time.monotonic()
            label = ("CHECK" if artifacts is not None else "DEFER HARNESS") if phase == "build" and any(k in case for k in HARNESS_KEYS) else phase.upper()
            print(f"{label} {name}", flush=True)
            try:
                for key, detail in setup_errors.items():
                    if key in case:
                        raise RuntimeError(detail)
                # The HTTP fixture serves a fixed port and may have mutable
                # state; serialize its clients while other cases run freely.
                lock = http_lock if phase == "run" and case.get("http") else contextlib.nullcontext()
                with lock:
                    options = {}
                    if name in prepared:
                        options["prepared_binary"], options["selector"] = prepared[name]
                    # Compiler/linker/VM checks exercise builds as assertions.
                    # Finish these checks in the build job, never the run job.
                    check = artifacts is not None and phase == "build" and any(k in case for k in HARNESS_KEYS)
                    ready = run_case(case, compiler, logs / name, env, timeout,
                                     vm=vm, phase="both" if check else phase,
                                     work=directories[name], **options)
                    if check:
                        ready = False
                return case, bool(ready), "", time.monotonic() - start
            except errors as exc:
                return case, False, str(exc), time.monotonic() - start

        def build_group(group, members):
            if len(members) == 1:
                return [stage(members[0], "build")]
            print(f"BUILD BATCH {group}: " + ", ".join(c["id"] for c in members), flush=True)
            completed, failure = build_batch(members, compiler, directories, logs, env, timeout, group)
            results = []
            for case in members:
                name = case["id"]
                if name in completed:
                    results.append((case, True, "", completed[name]))
                else:
                    results.append((case, False, f"{failure}; batch {group}", 0))
            return results

        def build_suite(name, members):
            start = time.monotonic()
            module_logs = logs / "modules" / name
            failure = ""
            try:
                build_module_suite(name, members, compiler,
                                   prepared[members[0][0]["id"]][0], module_logs, env, timeout)
            except errors as exc:
                failure = str(exc)
            seconds = time.monotonic() - start
            results = []
            for case, _ in members:
                directory = logs / case["id"]
                directory.mkdir(exist_ok=True)
                (directory / "build.module").write_text(f"modules/{name}\n")
                (directory / "build.status").write_text("1" if failure else "0")
                if failure:
                    for suffix in ("stdout", "stderr"):
                        source = module_logs / f"build.{suffix}"
                        if source.is_file():
                            shutil.copyfile(source, directory / f"build.{suffix}")
                results.append((case, not failure, failure, seconds / len(members)))
            return results

        groups = []
        eligible = []
        for case in cases:
            if case["id"] in prepared:
                continue
            if batch_size > 1 and batch_eligible(case) and not any(k in case for k in setup_errors):
                eligible.append(case)
                if len(eligible) == batch_size:
                    groups.append(eligible)
                    eligible = []
            else:
                groups.append([case])
        if eligible:
            groups.append(eligible)
        if suites:
            print(f"PLAN: {len(prepared)} cases in {len(suites)} module executables; "
                  f"{len(cases) - len(prepared)} independent/batch/harness cases", flush=True)
        ready = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = [pool.submit(build_suite, name, members) for name, members in suites.items()]
            futures += [pool.submit(build_group, i, group) for i, group in enumerate(groups)]
            for completed, future in enumerate(concurrent.futures.as_completed(futures), 1):
                group_results = future.result()
                print(f"BUILD PROGRESS {completed}/{len(futures)} groups: " +
                      ", ".join(f"{c['id']} ({seconds:.2f}s)" for c, _, _, seconds in group_results),
                      flush=True)
                for case, needs_run, detail, seconds in group_results:
                    elapsed[case["id"]] = seconds
                    if detail or not needs_run:
                        record(case, "FAIL" if detail else "PASS", detail, seconds)
                        shutil.rmtree(directories[case["id"]])
                    else:
                        ready.append(case)
        print(f"BUILD PHASE COMPLETE: {len(ready)} cases ready for execution", flush=True)
        if artifacts is not None:
            publications = []
            for case in ready:
                name = case["id"]
                binary, selector = prepared.get(name, (directories[name] / executable_name("program"), None))
                publications.append({"id": name,
                                     "directory": directories[name].relative_to(artifacts).as_posix(),
                                     "binary": binary.relative_to(artifacts).as_posix(),
                                     "selector": selector})
            return {
                "ready": publications,
                "http": http_binary.relative_to(artifacts).as_posix() if http_binary is not None else None,
            }
        server = None
        try:
            # This tooling fixture starts its own runner/server on port 18080.
            # Execute it before starting our shared HTTP server, even on a
            # single shard, to avoid a collision with the nested fixture.
            isolated = [c for c in ready if c["id"] == "kiem_tra_regression_runner"]
            for c in isolated:
                case, _, detail, seconds = stage(c, "run")
                record(case, "FAIL" if detail else "PASS", detail, elapsed[case["id"]] + seconds)
                shutil.rmtree(directories[case["id"]])
                ready.remove(c)
            if http_binary is not None:
                try:
                    server = start_prepared_http(http_binary, http_dir, env)
                except errors as exc:
                    setup_errors["http"] = str(exc)
            with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
                for future in concurrent.futures.as_completed(
                        [pool.submit(stage, c, "run") for c in ready]):
                    case, _, detail, seconds = future.result()
                    record(case, "FAIL" if detail else "PASS", detail,
                           elapsed[case["id"]] + seconds)
                    shutil.rmtree(directories[case["id"]])
        finally:
            stop_http(server)


def artifact_inputs(compiler, jit=None):
    """Reject artifacts from a different compiler, test corpus or tool version."""
    digest = hashlib.sha256()
    paths = [MANIFEST, Path(__file__).resolve()]
    paths += [p for root in (ROOT / "gói", TESTS) for p in root.rglob("*")
              if p.is_file() and p.suffix in (".vi", ".expected")
              and not any(part.startswith(".") for part in p.relative_to(ROOT).parts)]
    for path in sorted(paths):
        digest.update(path.relative_to(ROOT).as_posix().encode("utf-8") + b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    backend = compiler.with_name(executable_name("vpp-native"))
    return {"version": 1, "system": platform.system(), "machine": platform.machine().lower(),
            "compiler": hashlib.sha256(compiler.read_bytes()).hexdigest(),
            "backend": hashlib.sha256(backend.read_bytes()).hexdigest() if backend.is_file() else None,
            "sources": digest.hexdigest(),
            "jit": hashlib.sha256(jit.read_bytes()).hexdigest() if jit is not None else None}


def run_prepared_regression(cases, compiler, artifacts, plan, logs, env, timeout, record, jit=None):
    """Run saved AOT binaries and JIT sources in fresh workspaces."""
    ready = {c["id"]: c for c in plan["ready"]}
    checked = {c["id"]: c for c in plan["checks"]}
    jit_ids = set(plan.get("jit_cases", []))
    selected_jit = {c["id"] for c in cases if jit is not None and c.get("jit")}
    if jit_ids.intersection(c["id"] for c in cases) != selected_jit or jit_ids & (ready.keys() | checked.keys()):
        raise RuntimeError("JIT cases do not match the test artifact")
    missing = {c["id"] for c in cases} - (ready.keys() | checked.keys() | jit_ids)
    if missing:
        raise RuntimeError(f"cases missing from build artifact: {sorted(missing)}")

    def artifact_file(relative):
        path = (artifacts / relative).resolve()
        if not path.is_relative_to(artifacts) or not path.is_file():
            raise RuntimeError(f"missing artifact file: {relative}; rebuild the test artifact")
        return path

    # Fail before running anything if an executable/helper is absent. Never
    # fall back to compilation, even when the compiler is available.
    for case in cases:
        if case["id"] in ready:
            item = ready[case["id"]]
            artifact_file(item["binary"])
            if case.get("dns_mock"):
                artifact_file(item["directory"] + "/" + executable_name("dns-server"))
    http_binary = artifact_file(plan["http"]) if any(c.get("http") and c["id"] in ready for c in cases) else None
    errors = (OSError, RuntimeError, AssertionError, struct.error, subprocess.TimeoutExpired)
    server = None
    http_error = None
    # The nested runner starts its own HTTP server on the shared port.
    cases = sorted(cases, key=lambda c: c["id"] != "kiem_tra_regression_runner")
    with tempfile.TemporaryDirectory(prefix="vpp-regression-run-") as temporary:
        work = Path(temporary)
        try:
            for index, case in enumerate(cases):
                name = case["id"]
                if name in checked:
                    result = checked[name]
                    record(case, result["status"], "checked during build: " + result["detail"], 0)
                    continue
                start = time.monotonic()
                print(f"RUN {name} ({'JIT' if name in jit_ids else 'prebuilt'})", flush=True)
                case_work = work / str(index)
                try:
                    if name in jit_ids:
                        try:
                            run_case(case, compiler, logs / name, env, timeout, phase="run", jit=jit)
                            status, detail = "PASS", "JIT"
                        except errors as exc:
                            status, detail = "FAIL", f"JIT: {exc}"
                        record(case, status, detail, time.monotonic() - start)
                        continue
                    if case.get("http") and server is None:
                        if http_error:
                            raise RuntimeError(http_error)
                        directory = work / "http-fixture"
                        directory.mkdir(exist_ok=True)
                        binary = directory / executable_name("server")
                        shutil.copy2(http_binary, binary)
                        try:
                            server = start_prepared_http(binary, directory, env)
                        except errors as exc:
                            http_error = str(exc)
                            raise
                    item = ready[name]
                    directory = case_work
                    shutil.copytree(artifacts / item["directory"], directory)
                    binary = directory / executable_name("program")
                    # Shared module executables live above the case workspace.
                    shutil.copy2(artifact_file(item["binary"]), binary)
                    run_case(case, compiler, logs / name, env, timeout, phase="run",
                             work=directory, prepared_binary=binary, selector=item["selector"])
                    status, detail = "PASS", ""
                except errors as exc:
                    status, detail = "FAIL", str(exc)
                finally:
                    shutil.rmtree(case_work, ignore_errors=True)
                record(case, status, detail, time.monotonic() - start)
        finally:
            stop_http(server)
            http_log = work / "http-fixture/server.log"
            if http_log.is_file():
                destination = logs / "http-fixture"
                destination.mkdir(exist_ok=True)
                shutil.copy2(http_log, destination / "server.log")


def direct_regression(cases, compiler, jit, logs, env, timeout, jobs, record, *, jit_only=False, vm=None):
    """Run sources independently; never build or fall back for a JIT case."""
    errors = (OSError, RuntimeError, AssertionError, struct.error, subprocess.TimeoutExpired)
    guests = [c for c in cases if jit_eligible(c, jit_only)]
    checks = [c for c in cases if not jit_eligible(c, jit_only)]
    if jit_only:
        print(f"PLAN: {len(guests)} direct JIT cases, {len(checks)} cases requiring JIT migration; no builds", flush=True)
        for case in checks:
            record(case, "FAIL", "JIT migration required: " + toolchain_reason(case, jit_mode=True), 0)
        checks = []
    else:
        print(f"PLAN: {len(guests)} direct JIT cases, {len(checks)} AOT/toolchain cases", flush=True)
    server = None
    def run_guest(case):
        start = time.monotonic()
        print(f"START JIT {case['id']}", flush=True)
        try:
            run_case(case, compiler, logs / case["id"], env, timeout, jit=jit, vm=vm)
            return case, "PASS", "", time.monotonic() - start
        except errors as exc:
            return case, "FAIL", str(exc), time.monotonic() - start
    # HTTP has a fixed port. Keep the shared source server isolated from
    # other fixtures, especially tooling tests which start their own server.
    ordinary = [c for c in guests if not c.get("http")]
    http = [c for c in guests if c.get("http")]
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for result in concurrent.futures.as_completed([pool.submit(run_guest, c) for c in ordinary]):
            record(*result.result())
    if http:
        try:
            server = start_prepared_http([jit, "--dynamic", TESTS / "http_fixture.vi"],
                                         logs / "http-fixture", jit_environment(env), timeout)
            for c in http:
                record(*run_guest(c))
        except errors as exc:
            for c in http:
                record(c, "FAIL", f"JIT HTTP fixture: {exc}", 0)
        finally:
            stop_http(server)
    if checks:
        # Build unqualified fixtures once, then execute them without invoking
        # the compiler again. A failing qualified JIT case never enters here.
        staged_regression(checks, compiler, logs, env, timeout, jobs, record)


def print_failure_logs(directory):
    for name in ("build.stderr", "run.stderr", "run.stdout", "harness.stderr", "harness.stdout"):
        path = directory / name
        if path.is_file():
            with path.open("rb") as stream:
                stream.seek(max(0, path.stat().st_size - 4096))
                tail = stream.read().decode("utf-8", errors="replace").strip()
            if tail:
                print(f"  {name}: {tail}", flush=True)


def main():
    global FIXTURE_EXECUTABLE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture-executable", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--compiler", type=Path)
    parser.add_argument("--backend", choices=("jit", "aot", "toolchain", "mixed"),
                        help="jit: source-only, no builds; toolchain: only backend-specific checks; aot: all native checks; mixed: qualified JIT + AOT; default: JIT for all source suites, mixed/AOT for artifacts")
    parser.add_argument("--jit", type=Path,
                        help="path to packaged JIT (default: sibling of compiler or build-pure-vpp/bin/vpp-jit)")
    parser.add_argument("--vm", type=Path, help="packaged standalone VM (default: beside JIT)")
    parser.add_argument("--suite", choices=("full", "i32", "smoke", "host", "guest"), default="full")
    parser.add_argument("--case", action="append")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--inventory", action="store_true")
    parser.add_argument("--include-manual", action="store_true", help="include tests requiring public network/services; --case also opts in")
    parser.add_argument("--profile", action="store_true", help="save a separate compiler.profile for each case")
    parser.add_argument("--shard-count", type=int, default=1)
    parser.add_argument("--shard-index", type=int, default=0)
    parser.add_argument("--jobs", type=int, help="number of concurrent regression workers")
    parser.add_argument("--batch-size", type=int, choices=range(1, 5), default=1,
                        help="experimental full compiler AST reuse, up to four builds per process")
    parser.add_argument("--module-suites", action="store_true",
                        help="compile reviewed full regression cases into shared executables")
    parser.add_argument("--phase", choices=("all", "build", "run"), default="all",
                        help="build a reusable test artifact, or run it without compiling")
    parser.add_argument("--artifacts", type=Path,
                        help="test artifact directory for --phase build or --phase run")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--logs", type=Path, default=ROOT / "build-pure-vpp/regression-logs")
    args = parser.parse_args()
    if args.fixture_executable is not None:
        if (os.environ.get("VPP_TEST_FAKE") != "1" or
                not (ROOT / "scripts/quality/regression_runner_mock.py").is_file()):
            parser.error("--fixture-executable is reserved for the isolated runner self-test")
        FIXTURE_EXECUTABLE = args.fixture_executable.resolve()
        if not FIXTURE_EXECUTABLE.is_file():
            parser.error("runner fixture JIT is missing")
    if args.backend is None:
        if args.suite == "host":
            args.backend = "aot"
        else:
            args.backend = "jit" if args.phase == "all" else ("mixed" if args.suite != "i32" else "aot")
    if args.suite == "host" and args.backend != "aot":
        parser.error("trusted-host suite requires --backend aot")
    if args.suite == "guest" and args.backend != "jit":
        parser.error("dynamic-guest suite requires --backend jit")
    manifest = load_manifest()
    cases = [c for c in manifest["cases"] if (c["suite"] == args.suite or
             (args.suite == "smoke" and c.get("smoke")) or
             (args.suite == "full" and args.backend == "jit" and c["suite"] in ("i32", "guest")))]
    if args.case:
        unknown = set(args.case) - {c["id"] for c in cases}
        if unknown:
            parser.error(f"unknown case(s): {sorted(unknown)}")
        cases = [c for c in cases if c["id"] in args.case]
    if args.backend == "toolchain":
        cases = [c for c in cases if toolchain_reason(c)]
    if args.inventory:
        print(collections.Counter(c["suite"] + (" pending" if "pending" in c else " active") for c in manifest["cases"]))
        print(f"Unregistered source files: {len(manifest['unregistered_sources'])}")
    if args.list:
        for c in cases:
            label = (" [PENDING: " + c["pending"] + "]" if "pending" in c else "")
            if c.get("platforms"):
                label += " [" + ", ".join(c["platforms"]) + "]"
            if c.get("manual"):
                label += " [MANUAL: " + c["manual"] + "]"
            print(c["id"] + label)
        return 0
    if args.inventory:
        return 0
    if not 0 <= args.shard_index < args.shard_count or args.timeout < 1:
        parser.error("invalid shard or timeout")
    if args.jobs is not None and args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.batch_size > 1 and (args.suite != "full" or args.jobs is None):
        parser.error("--batch-size requires the full suite and --jobs")
    if args.module_suites and (args.suite != "full" or args.jobs is None):
        parser.error("--module-suites requires the full suite and --jobs")
    if (args.phase != "all") != (args.artifacts is not None):
        parser.error("--phase build/run requires --artifacts; --phase all does not accept it")
    direct = args.phase == "all" and args.backend not in ("aot", "toolchain") and not (args.backend == "mixed" and args.suite == "i32")
    if direct and (args.batch_size != 1 or args.module_suites):
        parser.error("AOT batching/module suites require --backend aot")
    if args.backend == "jit" and args.phase != "all":
        parser.error("direct JIT does not prepare test artifacts")
    if args.backend == "toolchain" and (args.phase != "all" or args.jit is not None):
        parser.error("toolchain checks require --phase all and no --jit")
    jit = args.jit.resolve() if args.jit is not None else None
    if jit is not None and not jit.is_file():
        parser.error(f"missing JIT: {jit}")
    if args.backend == "jit" and jit is None:
        jit = (args.compiler.resolve().with_name(executable_name("vpp-jit")) if args.compiler else
               ROOT / "build-pure-vpp/bin" / executable_name("vpp-jit"))
    if args.compiler is None and args.backend != "jit":
        parser.error("--compiler required")
    compiler = args.compiler.resolve() if args.compiler is not None else None
    if args.backend != "jit" and compiler is not None and not compiler.is_file():
        parser.error(f"missing compiler: {compiler}")
    if direct and jit is None:
        jit = compiler.with_name(executable_name("vpp-jit"))
    if direct and not jit.is_file():
        parser.error(f"missing JIT: {jit}")
    if args.backend == "jit" and args.shard_count != 1:
        parser.error("JIT regression must run as one suite")
    if args.shard_count > 1:
        shards = partition(cases, args.shard_count, args.module_suites)
        cases = shards[args.shard_index]
        print(f"SHARD {args.shard_index + 1}/{args.shard_count}: {len(cases)} registered cases", flush=True)
    else:
        print(f"SUITE {args.suite}: {len(cases)} registered cases", flush=True)
    host = platform.system()
    quiet_platform_skips = host in ("Linux", "Windows")
    pending = [c for c in cases if "pending" in c]
    unsupported = [c for c in cases if "pending" not in c and c.get("platforms") and host not in c["platforms"]]
    manual = [c for c in cases if c.get("manual") and c not in pending and c not in unsupported
              and not (args.include_manual or args.case)]
    if not quiet_platform_skips:
        for c in unsupported:
            print(f"UNSUPPORTED {c['id']} on {host}", flush=True)
    if not cases:
        parser.error("no cases in selection")
    cases = [c for c in cases if c not in pending and c not in unsupported and c not in manual]
    artifacts = args.artifacts.resolve() if args.artifacts is not None else None
    plan = None
    if args.phase == "build":
        if artifacts.exists() and any(artifacts.iterdir()):
            parser.error(f"test artifact directory must be empty: {artifacts}")
        artifacts.mkdir(parents=True, exist_ok=True)
    elif args.phase == "run":
        try:
            plan = json.loads((artifacts / "build.json").read_text(encoding="utf-8"))
            if plan["inputs"] != artifact_inputs(compiler, jit) or plan["suite"] != args.suite:
                parser.error("test artifact does not match this compiler, JIT, sources, suite or platform; rebuild it")
        except (OSError, ValueError, KeyError) as exc:
            parser.error(f"invalid test artifact: {exc}")
    env = dict(os.environ)
    # SQLite is a runtime dependency of the database driver, not a compiler.
    # Resolve it before JIT isolation strips PATH; retain the compiler guards.
    if not env.get("VPP_SQLITE3"):
        sqlite = shutil.which("sqlite3", path=env.get("PATH"))
        if sqlite:
            env["VPP_SQLITE3"] = str(Path(sqlite).resolve())
    installed = (jit if args.backend == "jit" else compiler).parent.parent / "share/vpp"
    env.setdefault("VPP_HOME", str(installed if (installed / "gói").is_dir() else ROOT))
    env.update(VPP_NATIVE_NO_CACHE="1", VPP_FFI_ENV_TEST="vpp-env-ffi-ok", VPP_FFI_ENV_EMPTY_TEST="")
    # Child quality scripts write UTF-8 logs even when Windows redirects stdout.
    env["PYTHONIOENCODING"] = "utf-8"
    env.pop("VPP_FFI_ENV_MISSING_TEST", None)
    logs = args.logs.resolve()
    logs.mkdir(parents=True, exist_ok=True)
    logs = Path(tempfile.mkdtemp(prefix="run-", dir=logs))
    if args.profile:
        env["VPP_COMPILER_PROFILE_FILE"] = str(logs / "compiler.profile")
    server = None
    http_error = None
    vm = None
    vm_error = None
    results = [{"id": c["id"], "status": "PENDING", "detail": c["pending"], "seconds": 0} for c in pending]
    # Preserve platform exclusions in the machine-readable report, even when
    # they are omitted from the Linux/Windows console output.
    results += [{"id": c["id"], "status": "UNSUPPORTED", "detail": host, "seconds": 0} for c in unsupported]
    results += [{"id": c["id"], "status": "MANUAL", "detail": c["manual"], "seconds": 0} for c in manual]
    for c in manual:
        print(f"MANUAL {c['id']}: {c['manual']} (--include-manual or --case to run)", flush=True)
    for c in pending:
        print(f"PENDING {c['id']}: {c['pending']}", flush=True)
    results_path = logs / "results.json"
    results_path.write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
    def record(case, status, detail, seconds):
        directory = logs / case["id"]
        seconds = round(seconds, 2)
        stages = {p.stem: float(p.read_text()) for p in directory.glob("*.seconds")}
        results.append({"id": case["id"], "status": status, "detail": detail,
                        "seconds": seconds, "stages": stages})
        results_path.write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
        timing = ", ".join(f"{name}={value:.2f}s" for name, value in sorted(stages.items()))
        print(f"{status} {case['id']} ({seconds:.2f}s; {timing}) {detail}", flush=True)
        if status == "FAIL":
            print_failure_logs(directory)

    if args.phase == "run":
        try:
            run_prepared_regression(cases, compiler, artifacts, plan, logs, env, args.timeout, record, jit=jit)
        except (OSError, RuntimeError, KeyError, TypeError) as exc:
            parser.error(f"cannot run test artifact: {exc}")
    elif args.phase == "build":
        jit_cases = [c["id"] for c in cases if jit is not None and c.get("jit")]
        aot_cases = [c for c in cases if c["id"] not in jit_cases]
        print(f"PLAN: {len(jit_cases)} JIT cases, {len(aot_cases)} AOT/harness cases", flush=True)
        plan = staged_regression(aot_cases, compiler, logs, env, args.timeout, args.jobs or 1, record,
                                 batch_size=args.batch_size, module_suites=args.module_suites,
                                 artifacts=artifacts)
        plan.update(inputs=artifact_inputs(compiler, jit), suite=args.suite, jit_cases=jit_cases,
                    checks=[r for r in results if r["status"] in ("PASS", "FAIL")])
        (artifacts / "build.json").write_text(json.dumps(plan, ensure_ascii=False, indent=2), encoding="utf-8")
        ready_ids = {c["id"] for c in plan["ready"]}
        for case in cases:
            if case["id"] in ready_ids:
                record(case, "BUILT", "saved for --phase run", 0)
            elif case["id"] in jit_cases:
                record(case, "JIT_READY", "source saved in checkout; run with packaged JIT", 0)
    elif direct:
        direct_regression(cases, compiler, jit, logs, env, args.timeout, args.jobs or 1, record,
                          jit_only=args.backend == "jit", vm=args.vm.resolve() if args.vm else None)
    elif args.jobs is not None:
        staged_regression(cases, compiler, logs, env, args.timeout, args.jobs, record,
                          batch_size=args.batch_size, module_suites=args.module_suites)
    else:
        try:
            for case in cases:
                start = time.monotonic()
                directory = logs / case["id"]
                print(f"START {case['id']}", flush=True)
                try:
                    if "vm_phase" in case and vm is None:
                        if vm_error:
                            raise RuntimeError(vm_error)
                        try:
                            vm = prepare_vm(compiler, logs, env, args.timeout)
                        except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
                            vm_error = str(exc)
                            raise
                    if case["id"] == "kiem_tra_regression_runner" and server is not None:
                        stop_http(server)
                        server = None
                    if case.get("http") and server is None:
                        if http_error:
                            raise RuntimeError(http_error)
                        try:
                            server = start_http(compiler, logs / "http-fixture", env, args.timeout)
                        except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
                            http_error = str(exc)
                            raise
                    run_case(case, compiler, directory, env, args.timeout, vm=vm,
                             jit=jit if case.get("jit") and args.backend != "aot" else None)
                    status, detail = "PASS", ""
                except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
                    status, detail = "FAIL", str(exc)
                seconds = round(time.monotonic() - start, 2)
                record(case, status, detail, seconds)
        finally:
            stop_http(server)
            results_path.write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
    failed = sum(c["status"] == "FAIL" for c in results)
    passed = sum(c["status"] == "PASS" for c in results)
    summary = f"{passed} passed, {failed} failed, {len(pending)} pending"
    if manual:
        summary += f", {len(manual)} manual (not run)"
    if args.phase == "build":
        summary = f"{len(plan['ready'])} built, {len(plan['jit_cases'])} JIT ready, {passed} build checks passed, {failed} failed, {len(pending)} pending"
    if not quiet_platform_skips:
        summary += f", {len(unsupported)} unsupported on this platform"
    print(f"{summary}; logs: {logs}")
    return int(failed != 0)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8", errors="backslashreplace")
    raise SystemExit(main())
