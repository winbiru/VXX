#!/usr/bin/env python3
"""Run V++/ASM regression fixtures against an existing native compiler."""
import argparse
import collections
import json
import os
import platform
import re
import signal
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "src/tests"
MANIFEST = TESTS / "regression.json"


def normalize(data):
    return data.replace(b"\r\n", b"\n")


def partition(cases, count):
    # Historical timings are scheduling hints only; the new backend can differ.
    weights = {}
    for line in (ROOT / "scripts/quality/native_golden_costs.tsv").read_text().splitlines():
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
    for case in sorted((c for c in cases if not c.get("http")),
                       key=lambda c: (-cost(c), c["id"])):
        index = min(range(count), key=lambda i: (totals[i], len(shards[i]), i))
        shards[index].append(case)
        totals[index] += cost(case)
    return shards


def load_manifest():
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    cases = manifest["cases"]
    ids = [case["id"] for case in cases]
    if len(ids) != len(set(ids)):
        raise ValueError("duplicate regression IDs")
    for kind, registered in (
        (".vi", {case["source"] for case in cases} | {source for case in cases for source in case.get("sources", [])} | set(manifest["unregistered_sources"])),
        (".expected", {case[key] for case in cases for key in ("expected", "historical_expected") if key in case}),
    ):
        actual = {p.relative_to(ROOT).as_posix() for p in TESTS.rglob("*" + kind)}
        actual = {p for p in actual if not any(part.startswith(".") for part in Path(p).parts)}
        if actual != registered:
            raise ValueError(f"{kind} inventory mismatch: new={sorted(actual-registered)}, missing={sorted(registered-actual)}")
    full = [c for c in cases if c["suite"] == "full"]
    shards = partition(full, 16)
    assigned = [c["id"] for shard in shards for c in shard]
    if (sorted(assigned) != sorted(c["id"] for c in full)
            or any(not any("pending" not in c for c in shard) for shard in shards)
            or any(c.get("http") for shard in shards[1:] for c in shard)):
        raise ValueError("invalid full regression shard assignment")
    return manifest


def execute(command, cwd, env, logs, name, timeout):
    with (logs / (name + ".stdout")).open("wb") as out, (logs / (name + ".stderr")).open("wb") as err:
        process = subprocess.Popen([str(x) for x in command], cwd=cwd, env=env,
                                   stdin=subprocess.DEVNULL, stdout=out, stderr=err,
                                   start_new_session=(os.name != "nt"))
        try:
            status = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            if os.name == "nt":
                process.kill()
            else:
                os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            raise
    (logs / (name + ".status")).write_text(str(status))
    return status


def build(compiler, source, binary, cwd, env, logs, timeout):
    return execute([compiler, "build", source, "-o", binary], cwd, env, logs, "build", timeout)


def verify_failure(case, status, logs, stage):
    if status != case["exit"]:
        raise RuntimeError(f"{stage} exit {status}, expected {case['exit']}")
    combined = (logs / (stage + ".stdout")).read_bytes() + (logs / (stage + ".stderr")).read_bytes()
    if case["diagnostic"].encode("utf-8") not in combined:
        raise RuntimeError(f"{stage} missing diagnostic: {case['diagnostic']}")


def run_case(case, compiler, logs, env, timeout):
    logs.mkdir(parents=True, exist_ok=True)
    env = dict(env)
    env.update(case.get("env", {}))
    if "timezone" in case:
        env["TZ"] = case["timezone"]
    with tempfile.TemporaryDirectory(prefix="vpp-regression-") as work:
        cwd = Path(work)
        env["VPP_PREFERENCES_HOME"] = str(cwd / "preferences")
        binary = cwd / ("program.exe" if os.name == "nt" else "program")
        source = ROOT / case["source"]
        if "dump" in case:
            for name in ("first", "second"):
                if execute([compiler, case["dump"], source], cwd, env, logs, name, timeout):
                    raise RuntimeError(f"{case['dump']} failed")
            first = (logs / "first.stdout").read_bytes()
            if not first.strip() or first != (logs / "second.stdout").read_bytes():
                raise RuntimeError(f"{case['dump']} produced empty or unstable output")
            return
        if "linker_phase" in case:
            status = execute([sys.executable, ROOT / "scripts/quality/check_native_regression_linkers.py",
                              case["linker_phase"], compiler, cwd], cwd, env, logs,
                             "harness", max(timeout, case.get("harness_timeout", 3600)))
            if status:
                raise RuntimeError(f"Linker {case['linker_phase']} harness exit {status}")
            return
        if "vm_phase" in case:
            cmd = ["cmake", f"-DVPP_STAGE1={compiler.with_name('vpp-i32')}",
                   f"-DVPP_NATIVE_COMPILER={compiler}", f"-DVPP_ROOT={ROOT}",
                   f"-DVPP_OUTPUT_DIR={cwd}", f"-DVPP_VM_PHASE={case['vm_phase']}",
                   "-P", ROOT / "scripts/quality/check_vm_asm.cmake"]
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
        status = build(compiler, source, binary, ROOT, env, logs, timeout)
        if case.get("compile_reject"):
            if status != 1 or binary.exists():
                raise RuntimeError(f"malformed source: expected build exit 1 and no executable; got {status}")
            return
        if status:
            if "diagnostic" in case and case.get("failure_phase") != "run":
                verify_failure(case, status, logs, "build")
                return
            raise RuntimeError(f"build exit {status}")
        if not binary.is_file():
            raise RuntimeError("compiler did not produce executable")
        if case["suite"] == "full":
            # Test programs may write relative paths; do not mutate the checkout.
            shutil.copytree(TESTS, cwd / "src/tests", ignore=shutil.ignore_patterns(".*"))
            shutil.copytree(ROOT / "gói", cwd / "gói")
        if case.get("dns_mock"):
            run_dns_mock(compiler, binary, cwd, env, logs, timeout)
            return
        status = execute([binary, *case.get("args", [])], cwd, env, logs, "run", timeout)
        if "diagnostic" in case:
            if case.get("failure_phase") == "build":
                raise RuntimeError("expected build rejection, but compiler produced an executable")
            verify_failure(case, status, logs, "run")
            return
        if status != case.get("exit", 0):
            raise RuntimeError(f"run exit {status}, expected {case.get('exit', 0)}")
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


def run_dns_mock(compiler, client, cwd, env, logs, timeout):
    # The legacy harness already checks the DNS server's query count and client output.
    i32 = compiler.with_name("vpp-i32")
    i32_env = dict(env)
    if platform.system() == "Linux":
        i32_env["VPP_NATIVE_ELF_LINK"] = "1"
    server = cwd / "dns-server"
    server_logs = logs / "server-build"
    server_logs.mkdir(exist_ok=True)
    if build(i32, TESTS / "fixtures/native_dns_mock.vi", server, ROOT, i32_env, server_logs, timeout):
        raise RuntimeError("DNS server build failed")
    failures = []
    for mode in (0, 1, 2, 3, 4, 10, 11, 12, 13, 14, 15, 16, 17, 20, 21, 30, 31, 32, 33, 34, 35, 36, 37):
        queries = 2 if mode in (0, 1, 3, 20, 21) else 1
        mode_logs = logs / f"mode-{mode}"
        mode_logs.mkdir(exist_ok=True)
        try:
            status = execute(["sh", ROOT / "scripts/quality/run_dns_vpp_mock.sh",
                              server, client, TESTS / "kiem_tra_dns_vpp_mock.vi", mode,
                              queries, mode_logs, "exe"], cwd, env, mode_logs, "dns", 30)
        except subprocess.TimeoutExpired:
            failures.append(f"mode {mode}: timed out")
            continue
        if status != 0 or normalize((mode_logs / "dns.stdout").read_bytes()) != b"VPP DNS MOCK OK\n":
            failures.append(f"mode {mode}: exit {status} or incorrect output")
    if failures:
        raise RuntimeError("DNS mock: " + "; ".join(failures))


def start_http(compiler, directory, env, timeout):
    directory.mkdir(parents=True, exist_ok=True)
    binary = directory / "server"
    if build(compiler, TESTS / "http_fixture.vi", binary, ROOT, env, directory, timeout):
        raise RuntimeError("HTTP fixture failed to compile")
    with (directory / "server.log").open("wb") as log:
        process = subprocess.Popen([str(binary)], cwd=directory, env=env, stdout=log, stderr=log)
    try:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline and process.poll() is None:
            try:
                with urllib.request.urlopen("http://127.0.0.1:18080/health", timeout=1) as response:
                    if json.loads(response.read()) == {"ok": True}:
                        return process
            except (OSError, ValueError):
                time.sleep(0.2)
        raise RuntimeError("HTTP fixture not ready on localhost:18080")
    except BaseException:
        stop_http(process)
        raise


def stop_http(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path)
    parser.add_argument("--suite", choices=("full", "i32", "smoke"), default="full")
    parser.add_argument("--case", action="append")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--inventory", action="store_true")
    parser.add_argument("--shard-count", type=int, default=1)
    parser.add_argument("--shard-index", type=int, default=0)
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--logs", type=Path, default=ROOT / "build-pure-vpp/regression-logs")
    args = parser.parse_args()
    manifest = load_manifest()
    cases = [c for c in manifest["cases"] if (c["suite"] == args.suite or
             (args.suite == "smoke" and c.get("smoke")))]
    if args.case:
        unknown = set(args.case) - {c["id"] for c in cases}
        if unknown:
            parser.error(f"unknown case(s): {sorted(unknown)}")
        cases = [c for c in cases if c["id"] in args.case]
    if args.inventory:
        print(collections.Counter(c["suite"] + (" pending" if "pending" in c else " active") for c in manifest["cases"]))
        print(f"Unregistered source files: {len(manifest['unregistered_sources'])}")
    if args.list:
        for c in cases:
            label = (" [PENDING: " + c["pending"] + "]" if "pending" in c else "")
            if c.get("platforms"):
                label += " [" + ", ".join(c["platforms"]) + "]"
            print(c["id"] + label)
        return 0
    if args.inventory:
        return 0
    if not 0 <= args.shard_index < args.shard_count or args.timeout < 1:
        parser.error("invalid shard or timeout")
    if args.compiler is None:
        parser.error("--compiler required")
    compiler = args.compiler.resolve()
    if not compiler.is_file():
        parser.error(f"missing compiler: {compiler}")
    cases = partition(cases, args.shard_count)[args.shard_index]
    pending = [c for c in cases if "pending" in c]
    unsupported = [c for c in cases if "pending" not in c and c.get("platforms") and platform.system() not in c["platforms"]]
    for c in unsupported:
        print(f"UNSUPPORTED {c['id']} on {platform.system()}", flush=True)
    if not cases:
        parser.error("no cases in selection")
    cases = [c for c in cases if c not in pending and c not in unsupported]
    env = dict(os.environ)
    installed = compiler.parent.parent / "share/vpp"
    env.setdefault("VPP_HOME", str(installed if (installed / "gói").is_dir() else ROOT))
    env.update(VPP_NATIVE_NO_CACHE="1", VPP_FFI_ENV_TEST="vpp-env-ffi-ok", VPP_FFI_ENV_EMPTY_TEST="")
    env.pop("VPP_FFI_ENV_MISSING_TEST", None)
    logs = args.logs.resolve()
    logs.mkdir(parents=True, exist_ok=True)
    logs = Path(tempfile.mkdtemp(prefix="run-", dir=logs))
    server = None
    http_error = None
    results = [{"id": c["id"], "status": "PENDING", "detail": c["pending"], "seconds": 0} for c in pending]
    results += [{"id": c["id"], "status": "UNSUPPORTED", "detail": platform.system(), "seconds": 0} for c in unsupported]
    for c in pending:
        print(f"PENDING {c['id']}: {c['pending']}", flush=True)
    try:
        for case in cases:
            start = time.monotonic()
            directory = logs / case["id"]
            try:
                if case.get("http") and server is None:
                    if http_error:
                        raise RuntimeError(http_error)
                    try:
                        server = start_http(compiler, logs / "http-fixture", env, args.timeout)
                    except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
                        http_error = str(exc)
                        raise
                run_case(case, compiler, directory, env, args.timeout)
                status, detail = "PASS", ""
            except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
                status, detail = "FAIL", str(exc)
            results.append({"id": case["id"], "status": status, "detail": detail,
                            "seconds": round(time.monotonic() - start, 2)})
            print(f"{status} {case['id']} {detail}", flush=True)
    finally:
        stop_http(server)
        (logs / "results.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
    failed = sum(c["status"] == "FAIL" for c in results)
    passed = sum(c["status"] == "PASS" for c in results)
    print(f"{passed} passed, {failed} failed, {len(pending)} pending, {len(unsupported)} unsupported on this platform; logs: {logs}")
    return int(failed != 0)


if __name__ == "__main__":
    raise SystemExit(main())
