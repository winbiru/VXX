#!/usr/bin/env python3
"""Regression checks for build-cache invalidation and test-runner configuration."""
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
QUALITY = ROOT / 'scripts/quality'


class MachoProbeTimeoutTests(unittest.TestCase):
    def test_cold_jit_probes_have_compilation_budget(self):
        spec = importlib.util.spec_from_file_location('macho_layout', QUALITY / 'check_native_macho_layout.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertEqual(module.probe_timeout(['vpp-jit', '--dynamic', 'probe.vi'], 30), 900)
        self.assertEqual(module.probe_timeout(['vpp-jit', '--dynamic', 'probe.vi'], 1800), 1800)
        self.assertEqual(module.probe_timeout(Path('probe'), 30), 30)


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
                                capture_output=True, encoding='utf-8', timeout=30)
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
                              capture_output=True, encoding='utf-8', timeout=30)

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
                              capture_output=True, encoding='utf-8', timeout=30)

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


class RegressionShardTests(unittest.TestCase):
    def test_manual_tests_require_opt_in(self):
        spec = importlib.util.spec_from_file_location('native_regression', QUALITY / 'run_native_regression.py')
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        cases = [dict(id='automatic', suite='full'),
                 dict(id='network', suite='full', manual='public network')]
        for options, expected in (([], ['automatic']), (['--include-manual'], ['automatic', 'network']),
                                  (['--case', 'network'], ['network'])):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as temporary:
                selected = []
                def direct(cases, *args, **kwargs):
                    selected.extend(c['id'] for c in cases)
                argv = ['runner', '--jit', str(Path(sys.executable)), '--logs', temporary, *options]
                with mock.patch.object(sys, 'argv', argv), \
                     mock.patch.object(runner, 'load_manifest', return_value={'cases': cases}), \
                     mock.patch.object(runner, 'direct_regression', side_effect=direct):
                    self.assertEqual(runner.main(), 0)
                self.assertEqual(selected, expected)
                results, = Path(temporary).glob('run-*/results.json')
                skipped = [c['id'] for c in json.loads(results.read_text()) if c['status'] == 'MANUAL']
                self.assertEqual(skipped, ['network'] if not options else [])

    def test_hosts_fixture_and_profiles_are_isolated(self):
        spec = importlib.util.spec_from_file_location('native_regression', QUALITY / 'run_native_regression.py')
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            observed = []
            def execute(command, cwd, env, logs, name, timeout):
                hosts = Path(env['VPP_DNS_VI_HOSTS_FILE'])
                self.assertTrue(hosts.is_relative_to(cwd))
                self.assertIn('203.0.113.21 fixture.test alias.test\n', hosts.read_text())
                self.assertEqual(env['VPP_DNS_VI_SERVER'], '')
                self.assertEqual(env['VPP_SOCKET_VI_ONLY'], '0')
                self.assertEqual(env['PATH'], runner.jit_environment({})['PATH'])
                profile = Path(env['VPP_COMPILER_PROFILE_FILE'])
                self.assertEqual(profile, logs / 'compiler.profile')
                observed.append(profile)
                (logs / 'run.stdout').write_bytes(b'ok\n')
                (logs / 'run.stderr').write_bytes(b'')
                return 0
            env = dict(os.environ, VPP_COMPILER_PROFILE_FILE=str(root / 'shared.profile'))
            case = dict(id='hosts', suite='i32', source='unused.vi', hosts_fixture=True, stdout='ok\n')
            with mock.patch.object(runner, 'execute', side_effect=execute):
                for index in range(2):
                    runner.run_case(case, None, root / str(index), env, 1, jit=Path(sys.executable))
            self.assertNotEqual(*observed)
            self.assertEqual(env['VPP_COMPILER_PROFILE_FILE'], str(root / 'shared.profile'))

    def test_reviewed_sources_skip_copies_and_edits_restore_isolation(self):
        spec = importlib.util.spec_from_file_location('native_regression', QUALITY / 'run_native_regression.py')
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        cases = runner.load_manifest()['cases']
        case = next(c for c in cases if c['id'] == 'anonymous_functions')
        for reviewed in runner.FIXTURE_FREE_SOURCES:
            matching = [c for c in cases if c['source'] == reviewed and runner.fixture_free(c, ROOT / reviewed)]
            self.assertTrue(matching, f'fixture-free review no longer matches: {reviewed}')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            checkout = root / 'checkout'
            tests = checkout / 'scripts/test'
            source = checkout / case['source']
            expected = checkout / case['expected']
            source.parent.mkdir(parents=True)
            expected.parent.mkdir(parents=True)
            (checkout / 'gói').mkdir()
            shutil.copyfile(ROOT / case['source'], source)
            shutil.copyfile(ROOT / case['expected'], expected)
            observed = []
            def execute(command, cwd, env, logs, name, timeout):
                self.assertEqual(command[1:3], ['--dynamic', source])
                self.assertEqual(Path(env['VPP_PREFERENCES_HOME']), cwd / 'preferences')
                observed.append(cwd)
                for relative in ('scripts/test', 'gói', 'fixtures', 'expected'):
                    self.assertEqual((cwd / relative).is_dir(), len(observed) > 1)
                if len(observed) > 1:
                    (cwd / 'scripts/test' / source.relative_to(tests)).write_bytes(b'private edit')
                    self.assertNotEqual(source.read_bytes(), b'private edit')
                (logs / 'run.stdout').write_bytes(expected.read_bytes())
                (logs / 'run.stderr').write_bytes(b'')
                return 0
            with mock.patch.object(runner, 'ROOT', checkout), \
                 mock.patch.object(runner, 'TESTS', tests), \
                 mock.patch.object(runner, 'execute', side_effect=execute):
                runner.run_case(case, None, root / 'first', os.environ, 1, jit=Path(sys.executable))
                source.write_bytes(source.read_bytes() + b'\n// changed\n')
                runner.run_case(case, None, root / 'second', os.environ, 1, jit=Path(sys.executable))
            self.assertNotEqual(*observed)
            self.assertFalse(any(cwd.exists() for cwd in observed))

    def test_raw_jit_abi_matches_both_assembly_resolvers(self):
        source = (ROOT / 'gói/biên dịch/JIT.vi').read_text(encoding='utf-8')
        arm = (ROOT / 'scripts/ASM/macos/aot_arm64/jit.inc').read_text(encoding='utf-8')
        x86 = (ROOT / 'scripts/ASM/common/vpp_jit_x86_64.inc').read_text(encoding='utf-8')
        mapping = {int(i): name for name, i in re.findall(
            r'nếu \(tên == "(vpp_native_\w+)"\) \{ id = (\d+); \}', source)}
        arm_mapping = {int(i): name for i, name in re.findall(
            r'Ljit_abi_(\d+):\n    adrp x24, _(vpp_native_\w+)@PAGE', arm)}
        x86_mapping = {int(i): name for i, name in re.findall(
            r'\.Lvpp_jit_abi_(\d+):\n    leaq (vpp_native_\w+)\(%rip\)', x86)}
        self.assertTrue(mapping)
        self.assertEqual(mapping, arm_mapping)
        self.assertEqual(mapping, x86_mapping)
        for i in mapping:
            self.assertIn(f'cmp w20, #{i}\n    b.eq Ljit_abi_{i}', arm)
            self.assertIn(f'cmpl ${i}, %r12d\n    je .Lvpp_jit_abi_{i}', x86)

    def test_full_jit_includes_i32_without_shards(self):
        spec = importlib.util.spec_from_file_location('native_regression', QUALITY / 'run_native_regression.py')
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        expected = [c['id'] for c in runner.load_manifest()['cases'] if c['suite'] in ('full', 'i32')]
        blocked = [(c['id'], runner.toolchain_reason(c, jit_mode=True))
                   for c in runner.load_manifest()['cases'] if c['id'] in expected
                   and runner.toolchain_reason(c, jit_mode=True)]
        self.assertEqual(blocked, [])
        result = subprocess.run([sys.executable, str(QUALITY / 'run_native_regression.py'),
                                 '--suite', 'full', '--backend', 'jit', '--list'],
                                capture_output=True, encoding='utf-8', check=True)
        self.assertCountEqual([line.split(' [')[0] for line in result.stdout.splitlines()], expected)

    def test_raw_jit_keeps_production_function_bodies(self):
        spec = importlib.util.spec_from_file_location('prepare_raw_jit', QUALITY / 'prepare_raw_jit.py')
        raw = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(raw)
        originals = {}
        paths = [ROOT / 'gói/biên dịch/bản địa/môi trường thực thi.vi',
                 ROOT / 'gói/biên dịch/bản địa/trình liên kết elf.vi']
        paths += [ROOT / 'gói/máy ảo' / (part + '.vi') for part in raw.VM_PARTS]
        stems = ('native_heap_allocator', 'native_directory_decode',
                 'native_elf_partial_linker_archive', 'native_elf_linker_archive')
        paths += [ROOT / 'scripts/test/fixtures' / (stem + '.vi') for stem in stems]
        for path in paths:
            for name, definition in raw.functions(path.read_text(encoding='utf-8'), path):
                if name != 'chính':
                    originals[name] = definition[0]
        with tempfile.TemporaryDirectory() as directory:
            for stem in stems:
                source = ROOT / 'scripts/test/fixtures' / (stem + '.vi')
                destination = raw.prepare(source, Path(directory) / (stem + '.vi'),
                                          vm_core=stem in stems[:2])
                selected = dict(raw.functions(destination.read_text(encoding='utf-8'), destination))
                self.assertIn('chính', selected)
                for name, definition in selected.items():
                    if name != 'chính':
                        self.assertEqual(definition[0], originals[name])
                self.assertNotIn('thực thi liên kết bên ngoài', selected)

    def test_raw_runtime_abi_matches_bootstrap_arities(self):
        abi = (ROOT / 'gói/biên dịch/abi lõi bản địa.vi').read_text(encoding='utf-8')
        parser = (ROOT / 'gói/biên dịch/bản địa/phân tích cú pháp.vi').read_text(encoding='utf-8')
        lowerer = (ROOT / 'gói/biên dịch/mã máy hạ biểu diễn trung gian.vi').read_text(encoding='utf-8')
        arities = {int(i): int(n) for i, n in re.findall(
            r'nếu \(mã định danh == (\d+)\) \{ trả về (\d+);', parser)}
        entries = re.findall(r'"id": (\d+), "symbol": "(vpp_native_\w+)", "arity": (\d+)', abi)
        for i, symbol, arity in entries:
            if int(i) in range(62, 76):
                self.assertEqual(int(arity), arities[int(i)], symbol)
                self.assertIn(f' nếu (mục ký hiệu == "{symbol}") {{ trả về {arity}; }};', lowerer)

    def test_diagnostics_are_eligible_for_jit(self):
        spec = importlib.util.spec_from_file_location('native_regression', QUALITY / 'run_native_regression.py')
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        cases = runner.load_manifest()['cases']
        diagnostic_cases = [case for case in cases if any(k in case for k in
                            ('run_diagnostic_regex', 'run_forbidden_regex', 'diagnostic', 'compile_reject'))]
        self.assertTrue(diagnostic_cases)
        self.assertTrue(all(runner.jit_eligible(case, True) for case in diagnostic_cases))

    def test_full_shards_keep_all_cases_and_module_groups(self):
        spec = importlib.util.spec_from_file_location('native_regression', QUALITY / 'run_native_regression.py')
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        cases = [c for c in runner.load_manifest()['cases'] if c['suite'] == 'full']
        for module_suites in (False, True):
            shards = runner.partition(cases, 4, module_suites)
            self.assertCountEqual([c['id'] for shard in shards for c in shard],
                                  [c['id'] for c in cases])
            self.assertTrue(all(shards))
            self.assertTrue(all(not c.get('http') for shard in shards[1:] for c in shard))
            for host in ('Darwin', 'Linux', 'Windows'):
                def active(selection):
                    return [c for c in selection if 'pending' not in c and
                            (not c.get('platforms') or host in c['platforms'])]
                applicable = active(cases)
                self.assertCountEqual([c['id'] for shard in shards for c in active(shard)],
                                      [c['id'] for c in applicable])
                if module_suites:
                    expected = runner.plan_module_suites(applicable)
                    actual = {}
                    for shard in shards:
                        for name, members in runner.plan_module_suites(active(shard)).items():
                            self.assertNotIn(name, actual)
                            actual[name] = [c['id'] for c, _ in members]
                    self.assertEqual(actual, {name: [c['id'] for c, _ in members]
                                              for name, members in expected.items()})

    def test_nested_runner_uses_http_port_before_outer_server(self):
        spec = importlib.util.spec_from_file_location('native_regression', QUALITY / 'run_native_regression.py')
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        cases = [{'id': 'http-client', 'suite': 'full', 'http': True},
                 {'id': 'kiem_tra_regression_runner', 'suite': 'full', 'tooling_test': True}]
        events = []
        def run_case(case, *args, phase, **kwargs):
            events.append((phase, case['id']))
            return True
        def start_http(*args):
            events.append(('start', 'http-server'))
            return object()
        with tempfile.TemporaryDirectory() as tmp, \
             mock.patch.object(runner, 'run_case', side_effect=run_case), \
             mock.patch.object(runner, 'prepare_http', return_value=Path(tmp) / 'server'), \
             mock.patch.object(runner, 'start_prepared_http', side_effect=start_http), \
             mock.patch.object(runner, 'stop_http'):
            record = mock.Mock()
            runner.staged_regression(cases, Path(tmp) / 'vpp', Path(tmp), {}, 1, 2, record)
        self.assertLess(events.index(('run', 'kiem_tra_regression_runner')),
                        events.index(('start', 'http-server')))
        self.assertLess(events.index(('start', 'http-server')),
                        events.index(('run', 'http-client')))
        self.assertEqual(record.call_count, 2)
        self.assertTrue(all(c.args[1] == 'PASS' for c in record.call_args_list))

    def test_prebuilt_helpers_and_nested_http_lifecycle_never_compile(self):
        spec = importlib.util.spec_from_file_location('native_regression', QUALITY / 'run_native_regression.py')
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        cases = [{'id': 'http-client', 'suite': 'full', 'http': True},
                 {'id': 'dns-client', 'suite': 'full', 'dns_mock': True},
                 {'id': 'kiem_tra_regression_runner', 'suite': 'full', 'tooling_test': True}]
        events = []
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            artifacts, logs = root / 'artifact', root / 'logs'
            logs.mkdir()
            ready = []
            for index, case in enumerate(cases):
                directory = artifacts / str(index)
                directory.mkdir(parents=True)
                binary = directory / runner.executable_name('program')
                binary.write_text('prebuilt')
                if case.get('dns_mock'):
                    (directory / runner.executable_name('dns-server')).write_text('DNS helper')
                ready.append(dict(id=case['id'], directory=str(index),
                                  binary=binary.relative_to(artifacts).as_posix(), selector=None))
            server = artifacts / runner.executable_name('server')
            server.write_text('HTTP helper')
            plan = dict(ready=ready, checks=[], http=server.name)

            def run_case(case, *args, phase, work, prepared_binary, **kwargs):
                self.assertEqual(phase, 'run')
                self.assertEqual(prepared_binary.read_text(), 'prebuilt')
                if case.get('dns_mock'):
                    self.assertEqual((work / runner.executable_name('dns-server')).read_text(), 'DNS helper')
                events.append(case['id'])

            def start_http(binary, *args):
                self.assertEqual(binary.read_text(), 'HTTP helper')
                events.append('start-http')
                return object()

            with mock.patch.object(runner, 'build', side_effect=AssertionError('unexpected compilation')), \
                 mock.patch.object(runner, 'run_case', side_effect=run_case), \
                 mock.patch.object(runner, 'start_prepared_http', side_effect=start_http), \
                 mock.patch.object(runner, 'stop_http') as stop:
                record = mock.Mock()
                runner.run_prepared_regression(cases, root / 'vpp', artifacts, plan, logs, {}, 1, record)
            self.assertEqual(events, ['kiem_tra_regression_runner', 'start-http', 'http-client', 'dns-client'])
            self.assertTrue(all(c.args[1] == 'PASS' for c in record.call_args_list))
            stop.assert_called_once()


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
                    '--suite', 'smoke', '--backend', 'mixed', '--compiler', str(compiler),
                    '--case', 'kiem_tra_aot_i32_run', '--timeout', '37', '--shard-count', '4',
                    '--shard-index', '2', '--phase', 'run', '--artifacts', str(package / 'tests'),
                    '--jit', str(package / 'bin/vpp-jit.exe'), '--logs', str(package / 'logs')]
            with mock.patch.object(sys, 'argv', argv), \
                 mock.patch.object(module.platform, 'system', return_value='Windows'), \
                 mock.patch.object(module.platform, 'machine', return_value='AMD64'), \
                 mock.patch.object(module.subprocess, 'call', return_value=7) as run:
                self.assertEqual(module.main(), 7)
            command = run.call_args.args[0]
            for option, value in (('--compiler', str(compiler.resolve())), ('--suite', 'smoke'), ('--backend', 'mixed'),
                                  ('--case', 'kiem_tra_aot_i32_run'), ('--timeout', '37'),
                                  ('--shard-count', '4'), ('--shard-index', '2'),
                                  ('--phase', 'run'), ('--artifacts', str((package / 'tests').resolve())),
                                  ('--jit', str((package / 'bin/vpp-jit.exe').resolve()))):
                self.assertEqual(command[command.index(option) + 1], value)
            env = run.call_args.kwargs['env']
            self.assertEqual(env['VPP_HOME'], (package.resolve() / 'share/vpp').as_posix())
            self.assertEqual(env['VPP_AOT_ASSEMBLER'], clang.resolve().as_posix())


@unittest.skipUnless(os.name == 'posix', 'Fake executables use a Python shebang')
class PreparedRegressionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='vpp prepared tests ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for name in ('scripts/quality', 'scripts/test/fixtures', 'gói', 'bin'):
            (self.root / name).mkdir(parents=True)
        for name in ('run_native_regression.py', 'native_golden_costs.tsv'):
            shutil.copyfile(QUALITY / name, self.root / 'scripts/quality' / name)
        cases = []
        names = ['kiem_tra_bo_qua', 'kiem_tra_boolean', 'kiem_tra_so_thuc',
                 *[f'regular{i}' for i in range(16)], 'reject', 'dump']
        for name in names:
            source = f'scripts/test/{name}.vi'
            (self.root / source).write_text('hàm chính() { in "ok"; };\n', encoding='utf-8')
            case = dict(id=name, source=source, suite='full')
            if name == 'regular0':
                case['jit'] = True
            if name == 'reject':
                case['compile_reject'] = True
            elif name == 'dump':
                case['dump'] = 'dump-ir'
            else:
                case['expected'] = f'scripts/test/{name}.expected'
                (self.root / case['expected']).write_text('ok\n')
            cases.append(case)
        (self.root / 'scripts/test/regression.json').write_text(
            json.dumps(dict(cases=cases, unregistered_sources={})), encoding='utf-8')
        self.compiler = self.root / 'bin/vpp'
        self.calls = self.root / 'compiler-calls'
        self.compiler.write_text(f'#!{sys.executable}\n' + '''import json, os, pathlib, sys
with open(os.environ['VPP_PREPARED_TEST_CALLS'], 'a') as log:
    log.write(json.dumps(sys.argv[1:]) + '\\n')
program = "#!" + sys.executable + "\\n" + """from pathlib import Path
import os
assert not Path('run-once').exists(), 'dirty test workspace'
Path('run-once').touch()
print('bad' if os.environ.get('VPP_PREPARED_TEST_FAIL') else 'ok')
"""
def publish(source, output):
    if pathlib.Path(source).stem == 'reject':
        sys.exit(1)
    path = pathlib.Path(output)
    path.write_text(program)
    path.chmod(0o755)
if sys.argv[1] == 'build':
    publish(sys.argv[2], sys.argv[4])
elif sys.argv[1] == 'batch-build':
    for index in range(2, len(sys.argv), 2):
        publish(sys.argv[index], sys.argv[index+1])
        print('VPP_BATCH_BUILD', index-1, 1)
else:
    print('stable compiler dump')
''')
        self.compiler.chmod(0o755)
        self.artifacts = self.root / 'build-artifact'

    def invoke(self, phase, *options, status=0, fail=False):
        result = subprocess.run([
            sys.executable, str(self.root / 'scripts/quality/run_native_regression.py'),
            '--compiler', str(self.compiler), '--phase', phase,
            *(['--artifacts', str(self.artifacts)] if phase != 'all' else []),
            '--logs', str(self.root / 'logs'), *options],
            env=dict(os.environ, VPP_PREPARED_TEST_CALLS=str(self.calls),
                     VPP_PREPARED_TEST_FAIL='1' if fail else ''),
            capture_output=True, encoding='utf-8', timeout=30)
        self.assertEqual(result.returncode, status, result.stdout + result.stderr)
        return result

    def test_relocated_artifact_runs_twice_without_compiler_or_dirty_workspace(self):
        options = ('--case', 'regular0', '--case', 'reject', '--case', 'dump')
        self.invoke('build', *options)
        self.assertNotIn('run-once', [p.name for p in self.artifacts.rglob('*')])
        calls = self.calls.read_text()
        relocated = self.root / 'extracted artifact'
        self.artifacts.rename(relocated)
        self.artifacts = relocated
        self.compiler.chmod(0o644)  # A run job cannot execute this compiler.
        for _ in range(2):
            result = self.invoke('run', *options)
            self.assertIn('3 passed, 0 failed', result.stdout)
            self.assertIn('checked during build', result.stdout)
        self.assertEqual(self.calls.read_text(), calls)
        self.assertFalse(any(p.name == 'run-once' for p in self.artifacts.rglob('*')))

    def test_batches_and_shared_modules_are_reused_and_runtime_failures_still_fail(self):
        options = tuple(arg for name in ('kiem_tra_bo_qua', 'kiem_tra_boolean', 'kiem_tra_so_thuc',
                                          'regular0', 'regular1') for arg in ('--case', name))
        self.invoke('build', *options, '--jobs', '1', '--batch-size', '2', '--module-suites')
        calls = [json.loads(line) for line in self.calls.read_text().splitlines()]
        self.assertEqual([c[0] for c in calls].count('batch-build'), 1)
        self.assertEqual([c[0] for c in calls].count('build'), 1)
        self.compiler.chmod(0o644)
        self.invoke('run', *options, status=1, fail=True)
        result = self.invoke('run', *options)
        self.assertIn('5 passed, 0 failed', result.stdout)
        self.assertEqual(len(self.calls.read_text().splitlines()), 2)

    def test_jit_runs_source_without_build_or_fallback(self):
        jit = self.root / 'bin/vpp-jit'
        jit_calls = self.root / 'jit-calls'
        jit.write_text(f'#!{sys.executable}\n' + """import os, pathlib, sys
assert sys.argv[1] == '--dynamic'
assert pathlib.Path(sys.argv[2]).is_absolute()
assert os.environ['VPP_AOT_ASSEMBLER'] == '/assembler-must-not-run'
assert os.environ['VPP_AOT_LINKER'] == '/linker-must-not-run'
assert not pathlib.Path('program').exists()
with open(""" + repr(str(jit_calls)) + """, 'a') as log:
    log.write(pathlib.Path(sys.argv[2]).stem + '\\n')
print('bad' if os.environ.get('VPP_PREPARED_TEST_FAIL') else 'ok')
""")
        jit.chmod(0o755)
        options = ('--case', 'regular0', '--case', 'regular1', '--jit', str(jit))
        self.invoke('build', *options)
        plan = json.loads((self.artifacts / 'build.json').read_text(encoding='utf-8'))
        self.assertEqual(plan['jit_cases'], ['regular0'])
        self.assertEqual([c['id'] for c in plan['ready']], ['regular1'])
        self.assertFalse(jit_calls.exists())
        calls = self.calls.read_text()
        self.assertEqual(len(calls.splitlines()), 1)
        self.compiler.chmod(0o644)
        for _ in range(2):
            result = self.invoke('run', *options)
            self.assertIn('2 passed, 0 failed', result.stdout)
        result = self.invoke('run', *options, status=1, fail=True)
        self.assertIn('FAIL regular0', result.stdout)
        self.assertEqual(jit_calls.read_text().splitlines(), ['regular0'] * 3)
        self.assertEqual(self.calls.read_text(), calls)
        with jit.open('a') as stream:
            stream.write('# changed JIT\n')
        self.invoke('run', *options, status=2)

    def test_direct_jit_checks_args_and_syntax_without_aot(self):
        manifest_path = self.root / 'scripts/test/regression.json'
        manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
        next(c for c in manifest['cases'] if c['id'] == 'regular1')['args'] = ['đối số']
        manifest_path.write_text(json.dumps(manifest), encoding='utf-8')
        jit = self.root / 'bin/vpp-jit'
        jit.write_text(f'#!{sys.executable}\n' + """import os, pathlib, sys
assert os.environ['VPP_AOT_ASSEMBLER'] == '/assembler-must-not-run'
assert not pathlib.Path('program').exists()
name = pathlib.Path(sys.argv[2]).stem
if name == 'reject':
    assert sys.argv[1] == '--check-dynamic'
    print('unrelated failure' if os.environ.get('VPP_PREPARED_TEST_FAIL') else 'V++ JIT: từ chối cú pháp')
    sys.exit(1)
assert sys.argv[1] == '--dynamic'
assert sys.argv[3:] == (['đối số'] if name == 'regular1' else [])
print('ok')
""", encoding='utf-8')
        jit.chmod(0o755)
        options = ('--backend', 'jit', '--jit', str(jit), '--jobs', '2',
                   '--case', 'regular0', '--case', 'regular1', '--case', 'reject')
        result = self.invoke('all', *options)
        self.assertIn('3 passed, 0 failed', result.stdout)
        self.assertFalse(self.calls.exists())
        result = self.invoke('all', *options, status=1, fail=True)
        self.assertIn('FAIL reject', result.stdout)
        self.assertFalse(self.calls.exists())

    def test_mixed_regression_routes_only_qualified_guests_to_jit(self):
        jit = self.root / 'bin/vpp-jit'
        jit.write_text(f'#!{sys.executable}\n' + """import os, pathlib, sys
name = pathlib.Path(sys.argv[2]).stem
if name == 'reject':
    assert sys.argv[1] == '--check-dynamic'
    print('V++ JIT: từ chối cú pháp')
    sys.exit(1)
assert name == 'regular0' and sys.argv[1] == '--dynamic'
print('bad' if os.environ.get('VPP_PREPARED_TEST_FAIL') else 'ok')
""", encoding='utf-8')
        jit.chmod(0o755)
        options = ('--backend', 'mixed', '--jit', str(jit), '--jobs', '2', '--case', 'regular0',
                   '--case', 'regular1', '--case', 'reject')
        result = self.invoke('all', *options)
        self.assertIn('2 direct JIT cases, 1 AOT/toolchain cases', result.stdout)
        self.assertIn('3 passed, 0 failed', result.stdout)
        calls = [json.loads(line) for line in self.calls.read_text().splitlines()]
        self.assertEqual(len(calls), 1)
        self.assertEqual(Path(calls[0][1]).stem, 'regular1')
        result = self.invoke('all', *options, status=1, fail=True)
        self.assertIn('FAIL regular0', result.stdout)
        calls = [json.loads(line) for line in self.calls.read_text().splitlines()]
        self.assertEqual([Path(call[1]).stem for call in calls], ['regular1', 'regular1'])

    def test_full_jit_runs_dump_without_toolchain(self):
        jit = self.root / 'bin/vpp-jit'
        jit.write_text(f'#!{sys.executable}\n' + """import os, pathlib, sys
assert sys.argv[1] == '--dynamic'
assert pathlib.Path(sys.argv[2]).stem in ('regular0', 'regular1', 'dump')
if pathlib.Path(sys.argv[2]).stem == 'dump':
    assert pathlib.Path(os.environ['VPP_TEST_SOURCE']).stem == 'dump'
    print('stable compiler dump')
else:
    print('bad' if os.environ.get('VPP_PREPARED_TEST_FAIL') else 'ok')
""")
        jit.chmod(0o755)
        selection = ('--case', 'regular0', '--case', 'regular1', '--case', 'dump')
        self.compiler.unlink()
        command = [sys.executable, str(self.root / 'scripts/quality/run_native_regression.py'),
                   '--jit', str(jit), '--logs', str(self.root / 'logs'), *selection]
        for failed in (False, True):
            result = subprocess.run(command, env=dict(os.environ, VPP_PREPARED_TEST_FAIL='1' if failed else ''),
                                    capture_output=True, encoding='utf-8', timeout=30)
            self.assertEqual(result.returncode, int(failed), result.stdout + result.stderr)
            self.assertIn('PASS dump', result.stdout)
            self.assertIn('3 direct JIT cases, 0 cases requiring JIT migration; no builds', result.stdout)
            self.assertFalse(self.calls.exists())

    def test_toolchain_mode_runs_only_backend_specific_checks(self):
        result = self.invoke('all', '--backend', 'toolchain', '--case', 'regular0', '--case', 'dump', '--jobs', '1')
        self.assertIn('1 passed, 0 failed', result.stdout)
        calls = [json.loads(line) for line in self.calls.read_text().splitlines()]
        self.assertEqual([call[0] for call in calls], ['dump-ir', 'dump-ir'])

    def test_missing_binary_fails_without_rebuild(self):
        options = ('--case', 'regular0')
        self.invoke('build', *options)
        calls = self.calls.read_text()
        plan = json.loads((self.artifacts / 'build.json').read_text(encoding='utf-8'))
        (self.artifacts / plan['ready'][0]['binary']).unlink()
        result = self.invoke('run', *options, status=2)
        self.assertIn('missing artifact file', result.stderr)
        self.assertEqual(self.calls.read_text(), calls)

    def test_changed_compiler_or_source_rejects_stale_artifact(self):
        options = ('--case', 'regular0')
        self.invoke('build', *options)
        source = self.root / 'scripts/test/regular0.vi'
        original = source.read_bytes()
        source.write_bytes(original + b'// changed\n')
        self.invoke('run', *options, status=2)
        source.write_bytes(original)
        with self.compiler.open('a') as stream:
            stream.write('# changed compiler\n')
        self.invoke('run', *options, status=2)
        self.assertEqual(len(self.calls.read_text().splitlines()), 1)


if __name__ == '__main__':
    unittest.main()
