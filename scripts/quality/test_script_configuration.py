#!/usr/bin/env python3
"""Regression checks for build-cache invalidation and test-runner configuration."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
QUALITY = ROOT / 'scripts/quality'


@unittest.skipUnless(os.name == 'posix', 'Bash bootstrap runs on POSIX hosts')
class RuntimeCacheTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='vpp-runtime-cache-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for relative in ('scripts/ASM/common', 'gói/biên dịch/bản địa', 'gói/lõi', 'tools'):
            (self.root / relative).mkdir(parents=True)
        for name in ('build-vpp-aot-runtime.sh', 'build-vpp-progress.sh'):
            shutil.copyfile(ROOT / 'scripts' / name, self.root / 'scripts' / name)
        self.seed = self.root / 'seed'
        self.seed.write_text(f'#!{sys.executable}\n' + '''import os, pathlib, shutil, sys
output = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])
if sys.argv[1] == 'build':
    with open(os.environ['VPP_CACHE_TEST_CALLS'], 'a') as log:
        log.write('build\\n')
    shutil.copyfile(__file__, output)
    output.chmod(0o755)
else:
    output.write_bytes(b'runtime object')
''')
        self.seed.chmod(0o755)
        # Exercise Linux validation without an actual compiler, linker or ELF.
        for name, source in {
            'uname': '#!/bin/sh\nif [ "$1" = -s ]; then echo Linux; else echo x86_64; fi\n',
            'readelf': '#!/bin/sh\nprintf "Type: REL (Relocatable file)\\nMachine: Advanced Micro Devices X86-64\\n"\n',
        }.items():
            path = self.root / 'tools' / name
            path.write_text(source)
            path.chmod(0o755)
        self.calls = self.root / 'calls'
        self.env = dict(os.environ, PATH=str(self.root / 'tools') + os.pathsep + os.environ['PATH'],
                        VPP_CACHE_TEST_CALLS=str(self.calls))
        self.run_build()

    def run_build(self):
        result = subprocess.run(['bash', str(self.root / 'scripts/build-vpp-aot-runtime.sh'),
                                 str(self.root / 'output'), str(self.seed)], env=self.env,
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return self.calls.read_text().count('build\n')

    def test_unchanged_inputs_reuse_compiler(self):
        self.assertEqual(self.run_build(), 1)

    def assert_dependency_rebuilds(self, relative):
        dependency = self.root / relative
        dependency.write_text('// changed dependency\n')
        newer = (self.root / 'output/vpp-i32').stat().st_mtime + 2
        os.utime(dependency, (newer, newer))
        self.assertEqual(self.run_build(), 2)

    def test_transitive_vpp_import_invalidates_compiler(self):
        self.assert_dependency_rebuilds('gói/lõi/transitive.vi')

    def test_asm_include_invalidates_compiler(self):
        self.assert_dependency_rebuilds('scripts/ASM/common/boundary.inc')


@unittest.skipUnless(os.name == 'posix', 'Source boundary audit runs in sh')
class SourceBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='vpp-source-archive-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'scripts/quality').mkdir(parents=True)
        self.audit = self.root / 'scripts/quality/audit_vi_asm_boundary.sh'
        shutil.copyfile(QUALITY / self.audit.name, self.audit)
        (self.root / 'gói').symlink_to(ROOT / 'gói', target_is_directory=True)
        (self.root / 'scripts/ASM').symlink_to(ROOT / 'scripts/ASM', target_is_directory=True)

    def run_audit(self):
        env = dict(os.environ)
        env.pop('VPP_AUDIT_BINARY', None)
        return subprocess.run(['sh', str(self.audit)], env=env,
                              capture_output=True, text=True, timeout=30)

    def test_archive_without_src_passes_without_find_errors(self):
        result = self.run_audit()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stderr, '')

    def test_archive_with_legacy_header_fails(self):
        (self.root / 'src').mkdir()
        (self.root / 'src/legacy.h').write_text('// forbidden legacy header\n')
        result = self.run_audit()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('src/legacy.h', result.stderr)


class OpcodeAbiTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='vpp-abi-check-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for relative in ('gói/biên dịch/mã thao tác.vi', 'gói/biên dịch/nội tại.vi',
                         'gói/biên dịch/asm chương trình.vi', 'gói/máy ảo/host ABI.vi'):
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)

    def check_abi(self):
        return subprocess.run(['cmake', '-DVPP_ROOT=' + str(self.root), '-P',
                               str(QUALITY / 'check_vpp_opcodes.cmake')],
                              capture_output=True, text=True, timeout=30)

    def test_source_archive_without_cpp_headers(self):
        result = self.check_abi()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_vm_numeric_abi_drift_is_rejected(self):
        path = self.root / 'gói/máy ảo/host ABI.vi'
        source = path.read_text(encoding='utf-8')
        self.assertIn('op == 99', source)
        path.write_text(source.replace('op == 99', 'op == 999', 1), encoding='utf-8')
        result = self.check_abi()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('ASM VM host ABI mismatch', result.stderr)


class WindowsRunnerTests(unittest.TestCase):
    def test_packaged_runner_preserves_selection_paths_and_exit_code(self):
        spec = importlib.util.spec_from_file_location('windows_bootstrap', QUALITY / 'check_windows_full_bootstrap.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(prefix='vpp windows runner ') as directory:
            package = Path(directory)
            clang = package / 'clang.exe'
            compiler = package / 'custom compiler.exe'
            clang.touch()
            compiler.touch()
            argv = ['runner', '--clang', str(clang), 'regression', str(package),
                    '--suite', 'smoke', '--compiler', str(compiler),
                    '--case', 'kiem_tra_aot_i32_run', '--timeout', '37', '--logs', str(package / 'logs')]
            with mock.patch.object(sys, 'argv', argv), \
                 mock.patch.object(module.platform, 'system', return_value='Windows'), \
                 mock.patch.object(module.platform, 'machine', return_value='AMD64'), \
                 mock.patch.object(module.subprocess, 'call', return_value=7) as run:
                self.assertEqual(module.main(), 7)
            command = run.call_args.args[0]
            for option, value in (('--compiler', str(compiler.resolve())), ('--suite', 'smoke'),
                                  ('--case', 'kiem_tra_aot_i32_run'), ('--timeout', '37')):
                self.assertEqual(command[command.index(option) + 1], value)
            env = run.call_args.kwargs['env']
            self.assertEqual(env['VPP_HOME'], (package.resolve() / 'share/vpp').as_posix())
            self.assertEqual(env['VPP_AOT_ASSEMBLER'], clang.resolve().as_posix())


if __name__ == '__main__':
    unittest.main()
