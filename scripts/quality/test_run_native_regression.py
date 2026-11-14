import contextlib
import io
import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest import mock

import run_native_regression as regression


class SharedVMTests(unittest.TestCase):
    def run_phases(self, packaged=False, build_status=0):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work).resolve()
            compiler = root / "bin/vpp"
            compiler.parent.mkdir()
            compiler.touch()
            if packaged:
                compiler.with_name("vpp-vm").touch()
            cases = [{"id": "vm-" + phase, "suite": "full",
                      "source": "unused.vi", "vm_phase": phase}
                     for phase in ("core", "directory", "process", "resolver",
                                   "socket_negative", "system", "tls")]

            def build_vm(compiler, source, binary, cwd, env, logs, timeout):
                if build_status == 0:
                    binary.touch()
                return build_status

            args = ["regression", "--compiler", str(compiler), "--logs", str(root / "logs")]
            with mock.patch.object(regression, "load_manifest", return_value={"cases": cases}), \
                    mock.patch.object(regression, "build", side_effect=build_vm) as build, \
                    mock.patch.object(regression, "run_case") as run, \
                    mock.patch("sys.argv", args), contextlib.redirect_stdout(io.StringIO()):
                status = regression.main()
            results = json.loads(next((root / "logs").rglob("results.json")).read_text())
            return compiler, status, build.call_args_list, run.call_args_list, results

    def test_seven_phases_build_one_vm_and_share_its_path(self):
        compiler, status, builds, runs, results = self.run_phases()
        self.assertEqual(status, 0)
        self.assertEqual(len(builds), 1)
        self.assertEqual(builds[0].args[0], compiler.with_name("vpp-i32"))
        self.assertEqual(builds[0].args[1], regression.ROOT / "gói/máy ảo/chính.vi")
        self.assertEqual(len(runs), 7)
        self.assertEqual({r.kwargs["vm"] for r in runs}, {builds[0].args[2]})
        self.assertEqual([r["status"] for r in results], ["PASS"] * 7)

    def test_packaged_vm_runs_every_phase_without_a_build(self):
        compiler, status, builds, runs, _ = self.run_phases(packaged=True)
        self.assertEqual(status, 0)
        self.assertEqual(builds, [])
        self.assertEqual(len(runs), 7)
        self.assertEqual({r.kwargs["vm"] for r in runs}, {compiler.with_name("vpp-vm")})

    def test_windows_vm_uses_native_executable_names(self):
        with tempfile.TemporaryDirectory() as work, mock.patch.object(regression.platform, "system", return_value="Windows"):
            root = Path(work)
            compiler = root / "vpp-native.exe"
            logs = root / "logs"
            def build_vm(compiler, source, binary, env, logs, **kwargs):
                binary.touch()
            with mock.patch("check_windows_full_bootstrap.raw_build", side_effect=build_vm) as build:
                vm = regression.prepare_vm(compiler, logs, {}, 900)
                self.assertEqual(vm.name, "vpp-vm.exe")
                self.assertEqual(build.call_args.args[0], root / "vpp-i32.exe")
                self.assertEqual(build.call_args.kwargs, {"vm_core": True, "timeout": 900})
            packaged = root / "vpp-vm.exe"
            packaged.touch()
            with mock.patch("check_windows_full_bootstrap.raw_build") as build:
                self.assertEqual(regression.prepare_vm(compiler, logs, {}, 900), packaged)
                build.assert_not_called()

    def test_failed_vm_setup_is_reported_without_seven_build_attempts(self):
        _, status, builds, runs, results = self.run_phases(build_status=1)
        self.assertEqual(status, 1)
        self.assertEqual(len(builds), 1)
        self.assertEqual(runs, [])
        self.assertEqual([r["status"] for r in results], ["FAIL"] * 7)
        self.assertTrue(all("Shared VM build failed" in r["detail"] for r in results))


    def test_vm_is_staged_beside_each_phases_dns_provider(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work).resolve()
            shared_vm = root / "shared-vm"
            shared_vm.write_bytes(b"prepared VM")
            logs = root / "logs"

            def harness(command, cwd, env, logs, name, timeout):
                staged = Path(next(arg.split("=", 1)[1] for arg in command
                                   if str(arg).startswith("-DVPP_VM=")))
                self.assertNotEqual(staged, shared_vm)
                self.assertEqual(staged.parent, cwd)
                self.assertEqual(staged.read_bytes(), shared_vm.read_bytes())
                return 0

            case = {"source": "unused.vi", "vm_phase": "resolver"}
            with mock.patch.object(regression, "execute", side_effect=harness) as run:
                regression.run_case(case, root / "vpp", logs, {}, 900, vm=shared_vm)
            self.assertEqual(run.call_count, 1)


class StagedRegressionTests(unittest.TestCase):
    def test_build_barrier_and_failure_isolation(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            cases = [{"id": name, "suite": "i32", "source": name + ".vi", "stdout": "ok\n"}
                     for name in ("first", "second", "reject", "broken")]
            cases[2]["compile_reject"] = True
            built = []
            ran = []
            records = {}
            lock = threading.Lock()

            def build(compiler, source, binary, cwd, env, logs, timeout):
                with lock:
                    built.append(source.stem)
                if source.stem in ("reject", "broken"):
                    return 1
                binary.write_text(source.stem)
                return 0

            def execute(command, cwd, env, logs, name, timeout):
                with lock:
                    self.assertCountEqual(built, [c["id"] for c in cases])
                    self.assertEqual(name, "run")
                    ran.append(Path(command[0]).read_text())
                (logs / "run.stdout").write_bytes(b"ok\n")
                return 0

            def record(case, status, detail, seconds):
                records[case["id"]] = status

            with mock.patch.object(regression, "build", side_effect=build), \
                    mock.patch.object(regression, "execute", side_effect=execute), \
                    contextlib.redirect_stdout(io.StringIO()):
                regression.staged_regression(cases, root / "vpp", root / "logs", {}, 10, 2, record)
            self.assertCountEqual(built, [c["id"] for c in cases])
            self.assertCountEqual(ran, ["first", "second"])
            self.assertEqual(records, {"first": "PASS", "second": "PASS", "reject": "PASS", "broken": "FAIL"})

    def test_shared_setup_once_and_http_clients_serialized(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            cases = [{"id": "vm", "vm_phase": "core"},
                     {"id": "http1", "http": True}, {"id": "http2", "http": True}]
            built = set()
            lock = threading.Lock()
            http_active = threading.Lock()
            server = object()

            def run_case(case, compiler, logs, env, timeout, *, vm, phase, work):
                self.assertEqual(vm, root / "vm")
                if phase == "build":
                    with lock:
                        built.add(case["id"])
                    return True
                self.assertEqual(built, {c["id"] for c in cases})
                if case.get("http"):
                    self.assertTrue(http_active.acquire(blocking=False))
                    http_active.release()

            with mock.patch.object(regression, "prepare_vm", return_value=root / "vm") as vm, \
                    mock.patch.object(regression, "prepare_http", return_value=root / "http") as http, \
                    mock.patch.object(regression, "start_prepared_http", return_value=server) as start, \
                    mock.patch.object(regression, "stop_http") as stop, \
                    mock.patch.object(regression, "run_case", side_effect=run_case), \
                    contextlib.redirect_stdout(io.StringIO()):
                record = mock.Mock()
                regression.staged_regression(cases, root / "vpp", root / "logs", {}, 10, 2, record)
            vm.assert_called_once()
            http.assert_called_once()
            start.assert_called_once()
            stop.assert_called_once_with(server)
            self.assertEqual(record.call_count, 3)
            self.assertTrue(all(call.args[1] == "PASS" for call in record.call_args_list))


class BatchRegressionTests(unittest.TestCase):
    def exercise_batch(self, markers, status=0, published=(0, 1), timeout=False):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            cases = [{"id": name, "suite": "full", "source": name + ".vi"}
                     for name in ("first", "second")]
            directories = {c["id"]: root / c["id"] for c in cases}
            for directory in directories.values():
                directory.mkdir()

            def execute(command, cwd, env, logs, name, limit):
                self.assertEqual(command[1], "batch-build")
                self.assertEqual(env["VPP_NATIVE_NO_CACHE"], "0")
                self.assertNotIn("VPP_COMPILER_PROFILE_FILE", env)
                self.assertEqual(limit, 20)
                for index in published:
                    Path(command[3 + index * 2]).touch()
                (logs / "batch.stdout").write_text(markers)
                (logs / "batch.stderr").write_text("compiler diagnostic")
                if timeout:
                    raise regression.subprocess.TimeoutExpired(command, limit)
                return status

            with mock.patch.object(regression, "execute", side_effect=execute):
                completed, failure = regression.build_batch(
                    cases, root / "vpp", directories, root / "logs", {}, 10, 0)
            remaining = [name for name, directory in directories.items()
                         if (directory / regression.executable_name("program")).exists()]
            return completed, failure, remaining

    def test_two_independent_publications(self):
        completed, failure, remaining = self.exercise_batch(
            "VPP_BATCH_BUILD 1 12.5\nVPP_BATCH_BUILD 3 2e3\n")
        self.assertEqual(completed, {"first": 0.0125, "second": 2.0})
        self.assertEqual(failure, "")
        self.assertEqual(remaining, ["first", "second"])

    def test_failure_never_accepts_unreported_executable(self):
        for status, timeout in ((1, False), (0, True), (0, False)):
            with self.subTest(status=status, timeout=timeout):
                completed, failure, remaining = self.exercise_batch(
                    "VPP_BATCH_BUILD 1 12\n", status, timeout=timeout)
                self.assertEqual(set(completed), {"first"})
                self.assertTrue(failure)
                self.assertEqual(remaining, ["first"])

    def test_nonzero_exit_after_all_markers_is_failure(self):
        completed, failure, remaining = self.exercise_batch(
            "VPP_BATCH_BUILD 1 12\nVPP_BATCH_BUILD 3 12\n", 1)
        self.assertEqual(set(completed), {"first"})
        self.assertTrue(failure)
        self.assertEqual(remaining, ["first"])

    def test_invalid_marker_or_missing_output(self):
        for marker, published in (("VPP_BATCH_BUILD 3 10", (0, 1)),
                                  ("VPP_BATCH_BUILD 1 NaN", (0, 1)),
                                  ("VPP_BATCH_BUILD 1 10", (1,))):
            with self.subTest(marker=marker, published=published):
                completed, failure, remaining = self.exercise_batch(marker, published=published)
                self.assertEqual(completed, {})
                self.assertTrue(failure)
                self.assertEqual(remaining, [])

    def test_batch_groups_keep_barrier_and_exclude_special_cases(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            cases = [{"id": name, "suite": "full", "source": name + ".vi"}
                     for name in ("first", "second", "reject", "third", "fourth")]
            cases[2]["compile_reject"] = True
            built, ran, records = set(), [], {}
            lock = threading.Lock()

            def batch(members, compiler, directories, logs, env, timeout, group):
                with lock:
                    names = {c["id"] for c in members}
                    self.assertNotIn("reject", names)
                    built.update(names)
                return {c["id"]: 0.1 for c in members}, ""

            def run_case(case, compiler, logs, env, timeout, *, vm, phase, work):
                with lock:
                    if phase == "build":
                        self.assertEqual(case["id"], "reject")
                        built.add(case["id"])
                        return False
                    self.assertEqual(built, {c["id"] for c in cases})
                    ran.append(case["id"])

            def record(case, status, detail, seconds):
                records[case["id"]] = status

            with mock.patch.object(regression, "build_batch", side_effect=batch) as batch_mock, \
                    mock.patch.object(regression, "run_case", side_effect=run_case), \
                    contextlib.redirect_stdout(io.StringIO()):
                regression.staged_regression(cases, root / "vpp", root / "logs",
                                             {}, 10, 2, record, batch_size=2)
            self.assertEqual(batch_mock.call_count, 2)
            self.assertCountEqual(ran, ["first", "second", "third", "fourth"])
            self.assertEqual(records, {c["id"]: "PASS" for c in cases})


if __name__ == "__main__":
    unittest.main()
