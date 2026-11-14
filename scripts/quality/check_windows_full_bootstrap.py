#!/usr/bin/env python3
"""Native Windows full V++ bootstrap, packaging and regression entry point."""
import argparse
import gzip
import hashlib
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SEEDS = ROOT / 'bootstrap/windows-x86_64'
SOURCE = ROOT / 'gói/biên dịch/dòng lệnh AOT tự chủ.vi'
ASM = ROOT / 'scripts/ASM'
FIXTURES = ROOT / 'src/tests/fixtures'
EXPECTED = ROOT / 'src/tests/expected'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_seeds():
    pins = {}
    for line in (SEEDS / 'SHA256SUMS').read_text(encoding='ascii').splitlines():
        sha, name = line.split()
        if name in pins or len(sha) != 64 or any(c not in '0123456789abcdef' for c in sha):
            raise RuntimeError(f'Invalid or duplicate Windows seed pin: {name}')
        pins[name] = sha
    if set(pins) != {'vpp-i32.exe', 'vpp-full.S.gz'}:
        raise RuntimeError('Windows i32 and full assembly seed pins are required')
    for name, sha in pins.items():
        if digest(SEEDS / name) != sha:
            raise RuntimeError(f'Windows seed checksum mismatch: {name}')


def audit_pe(path, deterministic=True):
    # The external Windows linker emits a standard PE, not the internal i32
    # linker's fixed layout. Require an AMD64 executable, a valid entry point,
    # non-WX sections, and no timestamp in the deterministic full route.
    data = path.read_bytes()
    if len(data) < 256 or data[:2] != b'MZ':
        raise RuntimeError(f'Not a PE executable: {path}')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if pe < 64 or pe + 24 > len(data) or data[pe:pe+4] != b'PE\0\0':
        raise RuntimeError(f'Invalid PE header: {path}')
    machine, count, stamp, _, _, optional, flags = struct.unpack_from('<HHIIIHH', data, pe + 4)
    start = pe + 24
    sections = start + optional
    if (machine != 0x8664 or not flags & 2 or flags & 0x2000 or optional < 112
            or sections + count * 40 > len(data) or not count
            or struct.unpack_from('<H', data, start)[0] != 0x20b
            or (deterministic and stamp != 0)):
        raise RuntimeError(f'Invalid AMD64 PE executable or timestamp: {path}')
    entry = struct.unpack_from('<I', data, start + 16)[0]
    entry_ok = False
    for index in range(count):
        off = sections + index * 40
        size, address, raw_size, raw = struct.unpack_from('<IIII', data, off + 8)
        characteristics = struct.unpack_from('<I', data, off + 36)[0]
        if raw_size and (not raw or raw + raw_size > len(data)):
            raise RuntimeError(f'Invalid PE section bounds: {path}')
        if characteristics & 0xa0000000 == 0xa0000000:
            raise RuntimeError(f'Writable executable PE section: {path}')
        if address <= entry < address + max(size, raw_size) and characteristics & 0x20000000:
            entry_ok = True
    if not entry or not entry_ok:
        raise RuntimeError(f'Invalid PE entry point: {path}')


def clean_env():
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith('VPP_')}
    env.update(VPP_NATIVE_TARGET='windows-x86_64', VPP_NATIVE_NO_CACHE='1')
    return env


def full_env(home, runtime_dir, i32_boundary, clang):
    env = clean_env()
    env.update(VPP_HOME=home.as_posix(), VPP_AOT_ASSEMBLER=clang.as_posix(),
               VPP_AOT_VPP_OBJECT=(runtime_dir / 'runtime.o').as_posix(),
               VPP_AOT_CORE_OBJECT=(runtime_dir / 'runtime-core.o').as_posix(),
               VPP_AOT_PLATFORM_OBJECT=(runtime_dir / 'platform.o').as_posix(),
               VPP_WINDOWS_RUNTIME_OBJECT=i32_boundary.as_posix())
    return env


def command(args, env, log, timeout=2400, status=0, expected=None, cwd=ROOT):
    args = [str(x) for x in args]
    print(f'RUN {log.name}: {args[0]} {args[1] if len(args) > 1 else ""}', flush=True)
    with log.open('wb') as output, log.with_suffix('.stderr.log').open('wb') as errors:
        result = subprocess.run(args, cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                                stdout=output, stderr=errors, timeout=timeout)
    if result.returncode != status:
        raise RuntimeError(f'{log}: exit {result.returncode}, expected {status}\n' +
                           log.read_bytes()[-4000:].decode('utf-8', 'replace') + '\n' +
                           log.with_suffix('.stderr.log').read_bytes()[-4000:].decode('utf-8', 'replace'))
    if expected is not None and log.read_bytes().replace(b'\r\n', b'\n') != expected.replace(b'\r\n', b'\n'):
        raise RuntimeError(f'{log}: output mismatch')


def assemble(clang, source, output, env, log, *defines):
    command([clang, '-x', 'assembler-with-cpp', '-c', source, *defines,
             '-I' + str(ASM), '-I' + str(ASM / 'common'), '-o', output], env, log)


def link(clang, objects, output, env, log):
    command([clang, *objects, '-lshell32', '-lbcrypt', '-lws2_32', '-lntdll',
             '-Wl,--no-insert-timestamp', '-o', output], env, log)
    audit_pe(output)


def fixture_cases(directory):
    hello = directory / 'hello.vi'
    hello.write_text('hàm chính() { in 42; };\n', encoding='utf-8')
    yield 'hello', hello, b'42\n', 0
    yield 'path-kind', FIXTURES / 'native_path_kind.vi', b'NATIVE PATH KIND OK\n', 0
    yield 'vm', FIXTURES / 'vm_asm.vi', ('-2147483648\n-2147483648\n-1\n3.75\n3.0\ni\n[1, 9, 3]\n16\n41\n42\n9\n25\nswitch\nngoại lệ VM\nASM VM OK\n').encode(), 0
    yield 'dynamic', FIXTURES / 'native_dynamic_vpp.vi', ('42\n43\n2147483647\n3.75\n2\n🐱\n[1, 9, 3]\n9\n41\n43\n9\n0\n1\n2\nđúng\n[ngoại lệ, 42]\n44\n10\n[1, 9, 3]\nVPP DYNAMIC AOT OK\n').encode(), 0
    for name, stem, status in [('private-method', 'native_aot_private_method', 0),
                               ('direct-calls', 'native_i32_direct_calls', 0),
                               ('loop-stack-gc', 'native_loop_stack_gc', 0),
                               ('return-semantics', 'native_return_semantics', 42),
                               ('exit-scalar', 'native_exit_scalar', 42),
                               ('protected-loop', 'native_protected_loop', 0)]:
        expected = b'' if name == 'exit-scalar' else (EXPECTED / (stem + '.expected')).read_bytes()
        yield name, FIXTURES / (stem + '.vi'), expected, status


def check_fixtures(compiler, directory, env):
    directory.mkdir()
    command([compiler, '--version'], env, directory / 'version.log', timeout=120)
    for name, source, expected, status in fixture_cases(directory):
        binary = directory / (name + '.exe')
        command([compiler, 'build', source, '-o', binary], env, directory / (name + '.build.log'), timeout=600)
        audit_pe(binary)
        command([binary], env, directory / (name + '.run.log'), timeout=120, status=status, expected=expected,
                cwd=directory if name == 'path-kind' else ROOT)


def package_env(package, clang):
    home = package / 'share/vpp'
    return full_env(home, home / 'native-vpp-runtime', home / 'windows-runtime.obj', clang)


def bootstrap(out, prefix, archive, clang):
    verify_seeds()
    out.mkdir(parents=True, exist_ok=False)
    env = clean_env()
    if prefix is None:
        boundary = out / 'i32-boundary.obj'
        assemble(clang, ASM / 'windows/vpp_aot_primitives_windows_x86_64.S', boundary,
                 env, out / 'i32-boundary.log', '-DVPP_NATIVE_RUNTIME_STACK_ABI=1')
        command(['pwsh', '-NoProfile', '-File', ROOT / 'scripts/quality/check_windows_native_pe_selfhosting.ps1',
                 '-SeedCompiler', SEEDS / 'vpp-i32.exe', '-RuntimeObject', boundary,
                 '-OutputDirectory', out / 'i32-bootstrap'], env, out / 'i32-bootstrap.log')
        i32 = out / 'i32-bootstrap/stage3.exe'
        boundary = out / 'i32-bootstrap/windows-runtime.obj'
    else:
        i32, boundary = prefix / 'bin/vpp-i32.exe', prefix / 'windows-runtime.obj'
    if not i32.is_file() or not boundary.is_file():
        raise RuntimeError('Missing Windows i32 compiler or boundary')
    command([sys.executable, ROOT / 'scripts/quality/check_native_executable_layout.py', i32], env, out / 'i32-audit.log')
    env = full_env(ROOT, out, boundary, clang)
    runtime_source = ROOT / 'gói/máy ảo/runtime syscall.vi'
    command([i32, 'runtime-object', runtime_source, '-o', out / 'runtime.o'], env, out / 'runtime.log')
    # Windows i32 exports implicit and source units separately. The dynamic
    # AOT core must be regenerated from the source units, including f64.
    command([i32, 'object', runtime_source, '-o', out / 'runtime-core.o'], env, out / 'runtime-core.log')
    assemble(clang, ASM / 'windows/vpp_windows_full_runtime_x86_64.S', out / 'platform.o', env, out / 'platform.log')
    assemble(clang, ASM / 'windows/vpp_windows_full_runtime_x86_64.S', out / 'raw-platform.o', env,
             out / 'raw-platform.log', '-DVPP_WINDOWS_RAW_BOUNDARY=1')
    with gzip.open(SEEDS / 'vpp-full.S.gz', 'rb') as source, (out / 'seed.S').open('wb') as dest:
        shutil.copyfileobj(source, dest)
    assemble(clang, out / 'seed.S', out / 'seed.o', env, out / 'seed.log', '-DVPP_AOT_VPP_RUNTIME=1')
    objects = [out / name for name in ('runtime.o', 'runtime-core.o', 'platform.o')]
    stage1 = out / 'vpp-native-stage1.exe'
    link(clang, [out / 'seed.o', *objects], stage1, env, out / 'stage1-link.log')
    check_fixtures(stage1, out / 'stage1-fixtures', env)
    previous = stage1
    for stage in (2, 3):
        current = out / f'vpp-native-stage{stage}.exe'
        command([previous, 'build', SOURCE, '-o', current], env, out / f'stage{stage}-build.log')
        audit_pe(current)
        check_fixtures(current, out / f'stage{stage}-fixtures', env)
        previous = current
    stage2, stage3 = (out / f'vpp-native-stage{s}.exe' for s in (2, 3))
    if stage2.read_bytes() != stage3.read_bytes():
        raise RuntimeError('Windows full compiler stages 2 and 3 diverged')
    (out / 'convergence.sha256').write_text(''.join(f'{digest(p)}  {p.name}\n' for p in (stage2, stage3)), encoding='ascii')
    print('WINDOWS_FULL_BOOTSTRAP_CONVERGENCE=PASS', flush=True)
    package = out / 'package'
    home = package / 'share/vpp'
    (package / 'bin').mkdir(parents=True)
    (home / 'native-vpp-runtime').mkdir(parents=True)
    shutil.copy2(stage3, package / 'bin/vpp-native.exe')
    shutil.copy2(i32, package / 'bin/vpp-i32.exe')
    shutil.copy2(boundary, home / 'windows-runtime.obj')
    for obj in [*objects, out / 'raw-platform.o']:
        shutil.copy2(obj, home / 'native-vpp-runtime' / obj.name)
    shutil.copytree(ROOT / 'gói', home / 'gói')
    shutil.copytree(ASM, home / 'scripts/ASM')
    for metadata in ('vpp-language.json', 'vpp-libraries.json'):
        shutil.copy2(ROOT / metadata, home / metadata)
    # The standalone VM has its own V++ main and needs the full OS boundary,
    # not the i32 compiler's narrower PE import table or AOT metadata.
    command([i32, 'object', ROOT / 'gói/máy ảo/chính.vi', '-o', out / 'vm.o'], env, out / 'vm-build.log')
    link(clang, [out / 'vm.o', out / 'runtime.o', out / 'platform.o'], package / 'bin/vpp-vm.exe', env, out / 'vm-link.log')
    command([stage3, 'vm-build', ROOT / 'gói/máy ảo/nhà cung cấp DNS.vi',
             '-o', package / 'bin/dns-provider.vmb'], env, out / 'package-dns-provider.log')
    shutil.copy2(ROOT / 'scripts/vpp-windows-launcher.cmd', package / 'bin/vpp.cmd')
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED) as z:
        for path in sorted(package.rglob('*')):
            if path.is_file():
                z.write(path, path.relative_to(package).as_posix())
    extracted = out / 'extracted'
    with zipfile.ZipFile(archive) as z:
        z.extractall(extracted)
    env = package_env(extracted, clang)
    compiler = extracted / 'bin/vpp-native.exe'
    smoke = out / 'package smoke'
    smoke.mkdir()
    probe = smoke / 'package-probe.vi'
    probe.write_text('hàm chính() { in 42; };\n', encoding='utf-8')
    binary, bytecode = smoke / 'package-probe.exe', smoke / 'package-probe.vmb'
    command([compiler, 'build', probe, '-o', binary], env, out / 'package-build.log', cwd=smoke)
    audit_pe(binary)
    run_env = clean_env()
    run_env['PATH'] = str(Path(os.environ['SystemRoot']) / 'System32')
    command([binary], run_env, out / 'package-run.log', expected=b'42\n', cwd=smoke)
    command([compiler, 'vm-build', probe, '-o', bytecode], env, out / 'package-bytecode.log', cwd=smoke)
    command([extracted / 'bin/vpp-vm.exe', bytecode], run_env, out / 'package-vm.log',
            expected=b'42\n', cwd=smoke)
    # Resolve from a deterministic hosts file, loading the packaged sibling
    # provider from a different working directory without external DNS.
    dns_image = smoke / 'dns.vmb'
    hosts = smoke / 'hosts.txt'
    hosts.write_text('203.0.113.47 vpp.example\n2001:db8::1 vpp.example\n', encoding='ascii')
    command([compiler, 'vm-build', FIXTURES / 'vm_asm_resolver_dns.vi', '-o', dns_image],
            env, out / 'package-dns-build.log', cwd=smoke)
    dns_env = dict(run_env, VPP_DNS_VI_HOSTS_FILE=hosts.as_posix(), VPP_DNS_VI_SERVER='',
                   VPP_DNS_VI_SERVERS='', VPP_DNS_VI_RESOLV_CONF='')
    dns_log = out / 'package-dns-run.log'
    command([extracted / 'bin/vpp-vm.exe', dns_image], dns_env, dns_log, cwd=smoke)
    lines = dns_log.read_text(encoding='utf-8').splitlines()
    if (len(lines) != 3 or lines[2] != 'DNS VM VI OK' or '203.0.113.47' not in lines[:2]
            or not any(ip in lines[:2] for ip in ('2001:db8::1', '2001:db8:0:0:0:0:0:1'))):
        raise RuntimeError(f'{dns_log}: packaged DNS provider output mismatch')
    # Exercise the actual installed launcher with no repository VPP_HOME.
    launcher_env = clean_env()
    launcher_env['VPP_AOT_ASSEMBLER'] = clang.as_posix()
    launcher_env['PATH'] = str(extracted / 'bin') + os.pathsep + launcher_env['PATH']
    launched = smoke / 'launcher.exe'
    command([os.environ['COMSPEC'], '/d', '/c', 'vpp.cmd', 'build', probe, '-o', launched],
            launcher_env, out / 'package-launcher.log', cwd=smoke)
    audit_pe(launched)
    command([launched], run_env, out / 'package-launcher-run.log', expected=b'42\n', cwd=smoke)
    print('WINDOWS_FULL_PACKAGE=PASS', flush=True)


def raw_build(i32, source, output, env, logs, vm_core=False, timeout=600):
    # Raw i32 probes use direct Win64 calls, not the older internal PE linker
    # with its limited stack-ABI boundary and import table.
    logs.mkdir(parents=True, exist_ok=True)
    obj = logs / 'probe.obj'
    command([i32, 'object', source, '-o', obj], env, logs / 'object.log', timeout=timeout)
    runtime = Path(env['VPP_AOT_VPP_OBJECT'])
    platform_object = runtime.with_name('platform.o' if vm_core else 'raw-platform.o')
    link(Path(env['VPP_AOT_ASSEMBLER']), [obj, runtime, platform_object], output, env, logs / 'link.log')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', required=True, type=Path, help='Native Windows GNU-target clang.exe (MSYS2 CLANG64)')
    modes = parser.add_subparsers(dest='mode', required=True)
    build = modes.add_parser('bootstrap')
    build.add_argument('output', type=Path)
    build.add_argument('--i32-prefix', type=Path)
    build.add_argument('--archive', required=True, type=Path)
    regress = modes.add_parser('regression')
    regress.add_argument('package', type=Path)
    regress.add_argument('--suite', choices=('full', 'i32'), default='full')
    regress.add_argument('--jobs', type=int, default=2)
    regress.add_argument('--logs', required=True, type=Path)
    raw = modes.add_parser('raw-build')
    raw.add_argument('i32', type=Path)
    raw.add_argument('source', type=Path)
    raw.add_argument('output', type=Path)
    raw.add_argument('--vm-core', action='store_true')
    args = parser.parse_args()
    if platform.system() != 'Windows' or platform.machine().lower() not in ('amd64', 'x86_64'):
        parser.error('Native Windows x86-64 is required')
    clang = args.clang.resolve(strict=True)
    if args.mode == 'raw-build':
        env = dict(os.environ, VPP_AOT_ASSEMBLER=str(clang))
        raw_build(args.i32.resolve(), args.source.resolve(), args.output.resolve(), env,
                  args.output.resolve().with_suffix('.build-logs'), vm_core=args.vm_core)
        return 0
    if args.mode == 'bootstrap':
        bootstrap(args.output.resolve(), args.i32_prefix.resolve(strict=True) if args.i32_prefix else None,
                  args.archive.resolve(), clang)
    else:
        package = args.package.resolve(strict=True)
        if args.suite == 'i32':
            home = package / 'share/vpp'
            env = clean_env()
            env.update(VPP_HOME=home.as_posix(),
                       VPP_WINDOWS_RUNTIME_OBJECT=(home / 'windows-runtime.obj').as_posix())
            compiler = package / 'bin/vpp-i32.exe'
        else:
            env = package_env(package, clang)
            compiler = package / 'bin/vpp-native.exe'
        return subprocess.call([sys.executable, str(ROOT / 'scripts/quality/run_native_regression.py'),
                                '--suite', args.suite, '--compiler', str(compiler),
                                '--jobs', str(args.jobs),
                                '--logs', str(args.logs.resolve())], cwd=ROOT, env=env)
    return 0


if __name__ == '__main__':
    sys.exit(main())
