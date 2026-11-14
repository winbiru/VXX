#!/usr/bin/env python3
"""Run V++/ASM regression fixtures against an existing native compiler."""
import argparse
import collections
import concurrent.futures
import contextlib
import threading
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


def executable_name(name):
    return name + ".exe" if platform.system() == "Windows" else name


def partition(cases, count):
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
    start = time.monotonic()
    with (logs / (name + ".stdout")).open("wb") as out, (logs / (name + ".stderr")).open("wb") as err:
        process = subprocess.Popen([str(x) for x in command], cwd=cwd, env=env,
                                   stdin=subprocess.DEVNULL, stdout=out, stderr=err,
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


def prepare_vm(compiler, logs, env, timeout):
    # CI packages one VM for every regression shard. Local runs build it once.
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


def run_case(case, compiler, logs, env, timeout, vm=None, *, phase="both", work=None):
    logs.mkdir(parents=True, exist_ok=True)
    env = dict(env)
    env.update(case.get("env", {}))
    if "timezone" in case:
        env["TZ"] = case["timezone"]
    with (tempfile.TemporaryDirectory(prefix="vpp-regression-") if work is None
          else contextlib.nullcontext(work)) as work:
        cwd = Path(work)
        env["VPP_PREFERENCES_HOME"] = str(cwd / "preferences")
        binary = cwd / executable_name("program")
        source = ROOT / case["source"]
        if phase == "build" and any(key in case for key in ("dump", "linker_phase", "vm_phase", "cmake")):
            return True  # These harnesses exercise compilation as part of the test.
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
            cmd = ["cmake", f"-DVPP_STAGE1={compiler.with_name(executable_name('vpp-i32'))}",
                   f"-DVPP_NATIVE_COMPILER={compiler}", f"-DVPP_ROOT={ROOT}",
                   f"-DVPP_OUTPUT_DIR={cwd}", f"-DVPP_VM_PHASE={case['vm_phase']}"]
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
        if phase != "run":
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
        if not binary.is_file():
            raise RuntimeError("missing prepared executable")
        if case["suite"] == "full":
            # Test programs may write relative paths; do not mutate the checkout.
            shutil.copytree(TESTS, cwd / "src/tests", ignore=shutil.ignore_patterns(".*"))
            shutil.copytree(ROOT / "gói", cwd / "gói")
            shutil.copytree(TESTS / "fixtures", cwd / "fixtures", ignore=shutil.ignore_patterns(".*"))
        if case.get("dns_mock"):
            run_dns_mock(binary, cwd, env, logs)
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


def run_dns_mode(server, client, cwd, env, logs, mode, queries, image=None):
    # Keep the actual V++ server and client on every OS; Python only manages
    # their lifetime and checks the protocol between the test processes.
    config = logs / "config"
    config.write_bytes(f"{mode}\n{queries}\n".encode("ascii"))
    hosts = logs / "empty-hosts"
    hosts.touch()
    server_out = logs / "server.out"
    deadline = time.monotonic() + 30
    with config.open("rb") as inp, server_out.open("wb") as out, (logs / "server.err").open("wb") as err:
        worker = subprocess.Popen([str(server)], cwd=cwd, env={**env, "PATH": ""},
                                  stdin=inp, stdout=out, stderr=err)
        try:
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
            client_env = dict(env, VPP_DNS_MOCK_MODE=str(mode), VPP_DNS_VI_PORT=port.decode("ascii"),
                              VPP_DNS_VI_SERVER="127.0.0.1", VPP_DNS_VI_SERVERS="127.0.0.2,127.0.0.1" if mode == 4 else "",
                              VPP_DNS_VI_HOSTS_FILE=str(hosts), VPP_DNS_VI_RESOLV_CONF="", VPP_SOCKET_VI_ONLY="1")
            if image is not None:
                client_env["PATH"] = ""
            status = execute([client] if image is None else [client, image], cwd, client_env,
                             logs, "client", max(0.1, deadline - time.monotonic()))
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
        finally:
            if worker.poll() is None:
                worker.kill()
            worker.wait()


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


def run_dns_mock(client, cwd, env, logs):
    server = cwd / executable_name("dns-server")
    failures = []
    for mode in (0, 1, 2, 3, 4, 10, 11, 12, 13, 14, 15, 16, 17, 20, 21, 30, 31, 32, 33, 34, 35, 36, 37):
        queries = 2 if mode in (0, 1, 3, 20, 21) else 1
        mode_logs = logs / f"mode-{mode}"
        mode_logs.mkdir(exist_ok=True)
        try:
            run_dns_mode(server, client, cwd, env, mode_logs, mode, queries)
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


def start_prepared_http(binary, directory, env):
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


def staged_regression(cases, compiler, logs, env, timeout, jobs, record):
    """Build each fixture once, then execute prepared binaries in parallel.

    Compiler/linker/VM harnesses intentionally compile their own inputs as
    part of their assertions. The ordinary AOT run phase never calls build.
    """
    errors = (OSError, RuntimeError, subprocess.TimeoutExpired)
    vm = None
    http_binary = None
    setup_errors = {}
    http_dir = logs / "http-fixture"
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
    # Keep executables off the diagnostics artifact and isolate every test.
    with tempfile.TemporaryDirectory(prefix="vpp-regression-build-") as work:
        work = Path(work)
        directories = {c["id"]: work / str(i) for i, c in enumerate(cases)}
        for directory in directories.values():
            directory.mkdir()

        def stage(case, phase):
            name = case["id"]
            start = time.monotonic()
            print(f"{phase.upper()} {name}", flush=True)
            try:
                for key, detail in setup_errors.items():
                    if key in case:
                        raise RuntimeError(detail)
                # The HTTP fixture serves a fixed port and may have mutable
                # state; serialize its clients while other cases run freely.
                lock = http_lock if phase == "run" and case.get("http") else contextlib.nullcontext()
                with lock:
                    ready = run_case(case, compiler, logs / name, env, timeout,
                                     vm=vm, phase=phase, work=directories[name])
                return case, bool(ready), "", time.monotonic() - start
            except errors as exc:
                return case, False, str(exc), time.monotonic() - start

        ready = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            for future in concurrent.futures.as_completed(
                    [pool.submit(stage, c, "build") for c in cases]):
                case, needs_run, detail, seconds = future.result()
                elapsed[case["id"]] = seconds
                if detail or not needs_run:
                    record(case, "FAIL" if detail else "PASS", detail, seconds)
                    shutil.rmtree(directories[case["id"]])
                else:
                    ready.append(case)
        print(f"BUILD PHASE COMPLETE: {len(ready)} cases ready for execution", flush=True)
        server = None
        try:
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path)
    parser.add_argument("--suite", choices=("full", "i32", "smoke"), default="full")
    parser.add_argument("--case", action="append")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--inventory", action="store_true")
    parser.add_argument("--shard-count", type=int, default=1)
    parser.add_argument("--shard-index", type=int, default=0)
    parser.add_argument("--jobs", type=int, help="staged build and run with this many concurrent workers")
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
    if args.jobs is not None and args.jobs < 1:
        parser.error("--jobs must be positive")
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
    vm = None
    vm_error = None
    results = [{"id": c["id"], "status": "PENDING", "detail": c["pending"], "seconds": 0} for c in pending]
    results += [{"id": c["id"], "status": "UNSUPPORTED", "detail": platform.system(), "seconds": 0} for c in unsupported]
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

    if args.jobs is not None:
        staged_regression(cases, compiler, logs, env, args.timeout, args.jobs, record)
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
                    if case.get("http") and server is None:
                        if http_error:
                            raise RuntimeError(http_error)
                        try:
                            server = start_http(compiler, logs / "http-fixture", env, args.timeout)
                        except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
                            http_error = str(exc)
                            raise
                    run_case(case, compiler, directory, env, args.timeout, vm=vm)
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
    print(f"{passed} passed, {failed} failed, {len(pending)} pending, {len(unsupported)} unsupported on this platform; logs: {logs}")
    return int(failed != 0)


if __name__ == "__main__":
    raise SystemExit(main())
