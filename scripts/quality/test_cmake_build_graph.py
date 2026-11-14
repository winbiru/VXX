"""Exercise the real CMake dependency graph with a small fake toolchain."""
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest


ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(platform.system() == "Darwin" and platform.machine() in {"arm64", "aarch64"}
                     and shutil.which("cmake") and shutil.which("make"),
                     "The native CMake project requires macOS ARM64 and Make")
class ParallelBuildGraphTests(unittest.TestCase):
    def test_shared_bootstrap_runs_once_and_preserves_incremental_dependencies(self):
        for target in (None, "vpp-vm"):
            with self.subTest(target=target), tempfile.TemporaryDirectory(prefix="vpp graph ") as work:
                source = Path(work).resolve() / "source"
                build = source.parent / "build"
                source.mkdir()
                shutil.copyfile(ROOT / "CMakeLists.txt", source / "CMakeLists.txt")
                for name in (
                    "bootstrap/macos-arm64/vpp-native", "bootstrap/macos-arm64/vpp-i32",
                    "bootstrap/macos-arm64/SHA256SUMS", "scripts/build-vpp-aot-runtime.sh",
                    "scripts/build-vpp-progress.sh", "scripts/vpp-native-launcher.sh.in",
                    "gói/máy ảo/chính.vi", "scripts/ASM/fixture.inc",
                    "vpp-language.json", "vpp-libraries.json",
                ):
                    path = source / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.touch()
                (source / "scripts/build-vpp-cli.sh").write_text(
                    '#!/bin/bash\nexec "$VPP_TEST_PYTHON" "$(dirname "$0")/fake_tools.py" bootstrap "$@"\n')
                (source / "scripts/fake_tools.py").write_text(textwrap.dedent('''\
                    from pathlib import Path
                    import sys
                    import time

                    mode = sys.argv[1]
                    output = Path(sys.argv[2] if mode == "bootstrap" else sys.argv[-1])
                    root = output if mode == "bootstrap" else output.parent.parent
                    root.mkdir(parents=True, exist_ok=True)
                    with (root / "invocations.log").open("a") as log:
                        log.write(mode + "\\n")
                    if mode == "bootstrap":
                        # A duplicate invocation fails immediately instead of
                        # corrupting the same staged compiler as on CI.
                        lock = root / "bootstrap-active"
                        lock.mkdir()
                        try:
                            time.sleep(0.3)
                            (root / "bin").mkdir(exist_ok=True)
                            (root / "native-vpp-runtime").mkdir(exist_ok=True)
                            for name in ("vpp-native", "vpp"):
                                (root / "bin" / name).write_text("compiler\\n")
                            i32 = root / "bin/vpp-i32"
                            i32.write_text('#!/bin/bash\\nexec "$VPP_TEST_PYTHON" "$VPP_HOME/scripts/fake_tools.py" vm "$@"\\n')
                            i32.chmod(0o755)
                            for name in ("runtime.o", "platform.o", "dynamic-bridge.o"):
                                (root / "native-vpp-runtime" / name).write_text("object\\n")
                        finally:
                            lock.rmdir()
                    else:
                        assert (root / "bin/vpp-native").is_file()
                        assert (root / "native-vpp-runtime/runtime.o").is_file()
                        output.write_text("shared VM\\n")
                        output.chmod(0o755)
                    '''))
                env = dict(os.environ, VPP_TEST_PYTHON=sys.executable)

                def run(*args):
                    result = subprocess.run([str(arg) for arg in args], env=env,
                                            capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

                def invocations():
                    return (build / "invocations.log").read_text().splitlines()

                def compile_project():
                    command = ["cmake", "--build", build, "--parallel", "8"]
                    if target:
                        command += ["--target", target]
                    run(*command)

                run("cmake", "-G", "Unix Makefiles", "-S", source, "-B", build, "-DBUILD_TESTING=OFF")
                compile_project()
                self.assertEqual(invocations(), ["bootstrap", "vm"])
                compile_project()
                self.assertEqual(invocations(), ["bootstrap", "vm"])

                # An updated compiler must rebuild the VM even when its own
                # source files have not changed.
                # Apple Make 3.81 compares timestamps at second resolution.
                time.sleep(1.05)
                (build / "bin/vpp-i32").touch()
                compile_project()
                self.assertEqual(invocations(), ["bootstrap", "vm", "vm"])

                time.sleep(1.05)
                (source / "gói/máy ảo/chính.vi").write_text("updated VM source\n")
                compile_project()
                self.assertEqual(invocations(), ["bootstrap", "vm", "vm", "bootstrap", "vm"])
                compile_project()
                self.assertEqual(invocations(), ["bootstrap", "vm", "vm", "bootstrap", "vm"])
                run("cmake", "--install", build, "--prefix", source.parent / "installed")
                self.assertEqual((source.parent / "installed/bin/vpp-vm").read_text(), "shared VM\n")


if __name__ == "__main__":
    unittest.main()
