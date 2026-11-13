import contextlib
import io
import json
from pathlib import Path
import tempfile
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


if __name__ == "__main__":
    unittest.main()
