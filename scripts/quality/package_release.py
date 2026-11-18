#!/usr/bin/env python3
"""Package the verified native prefix; never reuse release binaries or build caches."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unicodedata
import zipfile

ROOT = Path(__file__).resolve().parents[2]
PLATFORMS = {
    'macos-arm64': ('macos', 'arm64', 'vpp-macos.tar.gz'),
    'linux-x64': ('linux', 'x64', 'vpp-linux-x64.tar.gz'),
    'windows-x64': ('windows', 'x64', 'vpp-windows-x64.zip'),
}


def layout(platform):
    windows = platform == 'windows-x64'
    suffix = '.exe' if windows else ''
    launcher = '.cmd' if windows else ''
    return {'vpp': 'bin/vpp' + launcher, 'native': 'bin/vpp-native' + suffix,
            'i32': 'bin/vpp-i32' + suffix, 'vm': 'bin/vpp-vm' + suffix,
            'jit': 'bin/vpp-jit' + suffix, 'vdk': 'bin/vdk' + launcher,
            'vdkInfo': 'bin/vdk-info' + suffix, 'tools': 'bin/vpp-tools' + suffix}


def display_helper(platform):
    return 'bin/vdk-display.ps1' if platform == 'windows-x64' else 'bin/vdk-display.sh'


def required(platform):
    paths = [layout(platform)[k] for k in ('vpp', 'native', 'i32', 'vm', 'jit', 'tools')] + [
        'share/vpp/templates/application/src/chính.vi', 'share/vpp/templates/backend/application.vi',
        'bin/dns-provider.vmb', 'share/vpp/vpp-language.json',
        'vpp-install-manifest.tsv', 'bin/uninstall-native.ps1' if platform == 'windows-x64' else 'bin/uninstall-native.sh',
        'share/vpp/vpp-libraries.json', 'share/vpp/native-vpp-runtime/runtime.o',
        'share/vpp/scripts/graphics/CMakeLists.txt',
        'share/vpp/scripts/graphics/check_window.py',
        'share/vpp/scripts/graphics/check_gpu.py']
    runtime = 'share/vpp/native-vpp-runtime/'
    if platform == 'macos-arm64':
        paths += [runtime + x for x in ('platform.o', 'dynamic-bridge.o')]
        paths += ['bin/vpp-graphics-macos', 'bin/vpp-graphics-metal']
    elif platform == 'linux-x64':
        paths += ['share/vpp/scripts/quality/audit_linux_static_elf.sh']
        paths += [runtime + 'linux-objects/' + x + '.o' for x in
                  ('start', 'syscalls', 'runtime_core', 'directory', 'sha256', 'process', 'socket', 'aot_runtime_bridge', 'f64')]
    else:
        paths += [runtime + x for x in ('runtime-core.o', 'platform.o', 'raw-platform.o', 'vm-platform.o')]
        paths += ['share/vpp/windows-runtime.obj']
    return paths


def files(prefix):
    seen = set()
    for path in sorted(prefix.rglob('*')):
        relative = path.relative_to(prefix)
        if any(part in ('.vpp', '.git', '__pycache__', '.DS_Store') or part.startswith('._') for part in relative.parts):
            continue
        if path.is_symlink():
            raise ValueError(f'Unexpected symbolic link: {path}')
        if path.is_file():
            name = unicodedata.normalize('NFC', relative.as_posix())
            if name in seen:
                raise ValueError(f'Duplicate normalized path: {name}')
            seen.add(name)
            yield name, path


def validate_names(names, platform, sdk):
    names = set(names)
    expected = required(platform)
    if sdk:
        expected += [layout(platform)['vdk'], layout(platform)['vdkInfo'], display_helper(platform), 'INSTALL_EDITOR_HIGHLIGHT.md', 'vdk-release.json', 'LICENSE', 'README.md',
                     'docs/installation.md', 'docs/cli.md', 'docs/chuan-ngu-phap-tieng-viet.md', 'templates/application/README.md',
                     'examples/api_project/README.md', 'syntaxes/vpp.tmLanguage.json',
                     'language-configuration.json', 'assets/icons/vpp-file.png']
    forbidden = {n for n in names if Path(n).suffix.lower() in ('.c', '.cc', '.cpp', '.h', '.hpp', '.cxx')}
    if forbidden:
        raise ValueError(f'Unexpected C/C++ sources in native package: {sorted(forbidden)}')
    missing = set(expected) - names
    if missing or not any(n.startswith('share/vpp/gói/') and n.endswith('.vi') for n in names) or not any(n.startswith('share/vpp/scripts/ASM/') for n in names):
        raise ValueError(f'Incomplete package: missing {sorted(missing)} or stdlib/ASM')


def install_uninstaller(prefix, platform):
    name = 'uninstall-native.ps1' if platform == 'windows-x64' else 'uninstall-native.sh'
    shutil.copy2(ROOT / 'scripts/vdk' / name, prefix / 'bin' / name)
    if platform != 'windows-x64':
        (prefix / 'bin' / name).chmod(0o755)
    manifest = prefix / 'vpp-install-manifest.tsv'
    if manifest.is_symlink():
        raise ValueError(f'Unexpected symbolic link: {manifest}')
    entries = []
    for relative, path in files(prefix):
        if relative == 'vpp-install-manifest.tsv':
            continue
        if any(c in relative for c in '\t\r\n'):
            raise ValueError(f'Invalid installation path: {relative}')
        entries.append(f'{hashlib.sha256(path.read_bytes()).hexdigest()}\t{relative}\n')
    manifest.write_text(''.join(entries), encoding='utf-8', newline='\n')


def check_uninstaller(prefix, platform, sdk=False):
    """Run the real launchers against disposable, manifest-managed fixtures."""
    windows = platform == 'windows-x64'
    helper = 'uninstall-native.ps1' if windows else 'uninstall-native.sh'
    env = os.environ.copy()
    for key in ('VDK_HOME', 'VPP_HOME'):
        env.pop(key, None)
    with tempfile.TemporaryDirectory(prefix='vpp uninstall ') as directory:
        work = Path(directory)
        env.update(HOME=str(work), LOCALAPPDATA=str(work), ZDOTDIR=str(work))
        outside = work / 'outside.txt'
        outside.write_text('outside', encoding='utf-8')

        def fixture(name):
            root = work / name
            (root / 'bin').mkdir(parents=True)
            (root / 'share/vpp/gói').mkdir(parents=True)
            keys = ['vpp'] + (['vdk', 'vdkInfo'] if sdk else [])
            for relative in [layout(platform)[key] for key in keys] + ['bin/' + helper]:
                shutil.copy2(prefix / relative, root / relative)
            if sdk:
                shutil.copy2(prefix / display_helper(platform), root / display_helper(platform))
                shutil.copy2(prefix / 'vdk-release.json', root / 'vdk-release.json')
            managed = root / 'share/vpp/gói/tiếng Việt.vi'
            managed.write_text('hàm chính() { in "chào 🌿"; }\n', encoding='utf-8')
            (root / 'share/vpp/gói/binary.dat').write_bytes(bytes(range(256)) * 4096)
            (root / 'share/vpp/gói/empty.dat').write_bytes(b'')
            changed = root / 'share/changed.txt'
            changed.write_text('original', encoding='utf-8')
            install_uninstaller(root, platform)
            changed.write_text('user edit', encoding='utf-8')
            unrelated = root / 'project.vi'
            unrelated.write_text('project', encoding='utf-8')
            return root, managed, changed, unrelated

        def run(root, args, ok, via_call=False):
            executable = root / layout(platform)['vdk' if sdk else 'vpp']
            local_env = dict(env, PATH=str(root / 'bin') + os.pathsep + env['PATH'])
            entry = executable.name
            if via_call:
                assert windows
                caller = work / 'uninstall-caller.cmd'
                caller.write_text('@echo off\nsetlocal DisableDelayedExpansion\n'
                                  f'call {entry} %*\n'
                                  'set "VPP_TEST_RESULT=%errorlevel%"\n'
                                  'echo VPP_UNINSTALL_CALLER_CONTINUED\n'
                                  'exit /b %VPP_TEST_RESULT%\n', encoding='ascii', newline='\r\n')
                entry = caller.name
            command = [env['COMSPEC'], '/d', '/c', entry] if windows else [str(executable)]
            result = subprocess.run([*command, *args], cwd=work, env=local_env, capture_output=True, timeout=120)
            if (result.returncode == 0) != ok:
                raise RuntimeError(f'Uninstall {args}: exit {result.returncode}\n{result.stdout!r}\n{result.stderr!r}')
            if via_call and b'VPP_UNINSTALL_CALLER_CONTINUED' not in result.stdout:
                raise RuntimeError(f'Uninstall did not return to caller: {result.stdout!r}\n{result.stderr!r}')
            if windows and ok and result.stderr:
                raise RuntimeError(f'Unexpected uninstall stderr: {result.stderr!r}')

        def check_removed(root, managed, changed, unrelated):
            assert not managed.exists() and not (root / 'bin' / helper).exists()
            assert not (managed.parent / 'binary.dat').exists()
            assert not (managed.parent / 'empty.dat').exists()
            assert not (root / layout(platform)['vpp']).exists()
            if sdk:
                assert not (root / layout(platform)['vdk']).exists()
                assert not (root / layout(platform)['vdkInfo']).exists()
            assert not (root / 'vpp-install-manifest.tsv').exists()
            assert changed.read_text(encoding='utf-8') == 'user edit'
            assert unrelated.read_text(encoding='utf-8') == 'project'

        for i, args in enumerate((['uninstall'], ['gỡ cài đặt'], ['gỡ-cài-đặt'], ['gỡ', 'cài', 'đặt'], ['gỡ', 'cài đặt'])):
            root, managed, changed, unrelated = fixture(f'alias {i}')
            run(root, [*args, '--invalid'], False)
            assert managed.is_file()
            run(root, args, True)
            check_removed(root, managed, changed, unrelated)

        if windows:
            # An interactive shell or calling batch file must survive uninstall,
            # including its success/failure status after the launcher is deleted.
            root, managed, changed, unrelated = fixture('called launcher')
            run(root, ['uninstall'], True, via_call=True)
            check_removed(root, managed, changed, unrelated)
            root, managed, _, _ = fixture('called invalid manifest')
            (root / 'vpp-install-manifest.tsv').unlink()
            run(root, ['uninstall'], False, via_call=True)
            assert managed.is_file()

            # Exercise the same Windows PowerShell as the launcher, with the
            # hash cmdlet deliberately unusable even if installed on this host.
            root, managed, changed, unrelated = fixture('no hash cmdlet')
            # Windows os.environ normalizes keys to uppercase; its plain dict
            # copy no longer supports case-insensitive lookups.
            powershell = Path(env['SYSTEMROOT']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
            result = subprocess.run(
                [str(powershell), '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command',
                 "function Get-FileHash { throw 'Get-FileHash must not be called' }; "
                 "& (Join-Path $env:VPP_UNINSTALL_TEST_ROOT 'bin/uninstall-native.ps1') "
                 "-Root $env:VPP_UNINSTALL_TEST_ROOT"],
                cwd=work, env=dict(env, VPP_UNINSTALL_TEST_ROOT=str(root)), capture_output=True, timeout=120)
            if result.returncode != 0:
                raise RuntimeError(f'Uninstall without Get-FileHash: exit {result.returncode}\n{result.stdout!r}\n{result.stderr!r}')
            check_removed(root, managed, changed, unrelated)

        for i, invalid in enumerate(('missing', 'traversal', 'duplicate', 'malformed', 'empty')):
            root, managed, _, _ = fixture(f'invalid {i}')
            manifest = root / 'vpp-install-manifest.tsv'
            if invalid == 'missing':
                manifest.unlink()
            elif invalid == 'empty':
                manifest.write_bytes(b'')
            else:
                raw = manifest.read_bytes()
                extra = {'traversal': b'0' * 64 + b'\t../outside.txt\n',
                         'duplicate': raw.splitlines(keepends=True)[0],
                         'malformed': b'invalid\n'}[invalid]
                manifest.write_bytes(raw + extra)
            run(root, ['uninstall'], False)
            assert managed.is_file() and outside.read_text(encoding='utf-8') == 'outside'
        if not windows:
            root, managed, _, _ = fixture('symlink')
            managed.unlink()
            managed.symlink_to(outside)
            run(root, ['uninstall'], False)
            assert managed.is_symlink() and outside.read_text(encoding='utf-8') == 'outside'
    print(f'{"VDK" if sdk else "VPP"}_UNINSTALL_REGRESSION=PASS', flush=True)


def archive(prefix, destination, platform, sdk):
    entries = list(files(prefix))
    validate_names((name for name, _ in entries), platform, sdk)
    if destination.suffix == '.zip':
        with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED) as output:
            for name, path in entries:
                output.write(path, name)
        with zipfile.ZipFile(destination) as result:
            validate_names(result.namelist(), platform, sdk)
            if result.testzip() is not None:
                raise ValueError('Corrupt ZIP')
    else:
        with destination.open('wb') as raw, gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=0) as compressed, tarfile.open(fileobj=compressed, mode='w') as output:
            for name, path in entries:
                info = output.gettarinfo(str(path), arcname=name)
                info.uid = info.gid = 0
                info.uname = info.gname = ''
                info.mtime = 0
                with path.open('rb') as source:
                    output.addfile(info, source)
        with tarfile.open(destination) as result:
            validate_names(result.getnames(), platform, sdk)
    with destination.open('rb') as source:
        digest = hashlib.file_digest(source, 'sha256').hexdigest() if hasattr(hashlib, 'file_digest') else hashlib.sha256(source.read()).hexdigest()
    destination.with_name(destination.name + '.sha256').write_text(f'{digest}  {destination.name}\n', encoding='ascii')
    print(f'{destination.name}: {digest}', flush=True)


def build_sdk(prefix, platform):
    executables = layout(platform)
    home = prefix / 'share/vpp'
    env = os.environ.copy()
    # Clear build-tree settings so the SDK is linked against its own runtime.
    for key in list(env):
        if key.startswith('VPP_AOT_') or key in ('VPP_HOME', 'VPP_WINDOWS_RUNTIME_OBJECT', 'VPP_NATIVE_TARGET'):
            if key != 'VPP_AOT_ASSEMBLER':
                env.pop(key)
    env['VPP_HOME'] = home.as_posix()
    if platform == 'windows-x64':
        compiler = prefix / executables['native']
        runtime = home / 'native-vpp-runtime'
        env.update(VPP_AOT_VPP_OBJECT=(runtime / 'runtime.o').as_posix(),
                   VPP_AOT_CORE_OBJECT=(runtime / 'runtime-core.o').as_posix(),
                   VPP_AOT_PLATFORM_OBJECT=(runtime / 'platform.o').as_posix(),
                   VPP_WINDOWS_RUNTIME_OBJECT=(home / 'windows-runtime.obj').as_posix(),
                   VPP_NATIVE_TARGET='windows-x86_64',
                   VPP_AOT_ASSEMBLER=env.get('VPP_CI_CLANG', env.get('VPP_AOT_ASSEMBLER', 'clang.exe')))
    else:
        compiler = prefix / executables['vpp']
        if platform == 'linux-x64':
            env['VPP_AOT_LINUX_OBJECT_DIR'] = (home / 'native-vpp-runtime/linux-objects').as_posix()
    subprocess.run([str(compiler), 'build', str(ROOT / 'scripts/vdk/vdk.vi'), '-o', str(prefix / executables['vdkInfo'])], env=env, check=True)
    shutil.copy2(ROOT / 'scripts/vdk' / Path(executables['vdk']).name, prefix / executables['vdk'])
    helper = display_helper(platform)
    shutil.copy2(ROOT / 'scripts/vdk' / Path(helper).name, prefix / helper)
    if platform != 'windows-x64':
        (prefix / executables['vdk']).chmod(0o755)
        (prefix / helper).chmod(0o755)
    os_name, arch, _ = PLATFORMS[platform]
    version = json.loads((home / 'vpp-libraries.json').read_text(encoding='utf-8'))['sdkVersion']
    manifest = {'schema': 1, 'name': 'VDK', 'version': version, 'platform': os_name, 'arch': arch,
                'components': dict.fromkeys(('compiler', 'runtime', 'stdlib', 'tooling'), version),
                'executables': executables, 'libraryCatalog': 'share/vpp/vpp-libraries.json',
                'packageRoot': 'share/vpp/gói'}
    (prefix / 'vdk-release.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    for name in ('LICENSE', 'README.md'):
        shutil.copy2(ROOT / name, prefix / name)
    # Only ship tracked source assets, not local build products or ignored
    # caches. Source exports without Git use the same portable file types.
    directories = ('docs', 'templates', 'examples', 'syntaxes', 'themes', 'assets/icons')
    allowed = {'.md', '.bnf', '.vi', '.json', '.properties', '.lock', '.sh', '.png', '.svg'}
    if (ROOT / '.git').exists() and shutil.which('git'):
        names = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files', '-z', '--', *directories]).decode('utf-8').split('\0')
        names = sorted(set(names) | {'docs/chuan-ngu-phap-tieng-viet.md'})
        assets = ((name, ROOT / name) for name in names if name)
    else:
        assets = ((directory + '/' + name, source) for directory in directories
                  for name, source in files(ROOT / directory))
    for name, source in assets:
        if source.suffix.lower() not in allowed and source.name != '.gitignore':
            continue
        if source.is_symlink() or not source.is_file():
            raise ValueError(f'Invalid SDK asset: {source}')
        target = prefix / unicodedata.normalize('NFC', name)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    for name in ('language-configuration.json', 'INSTALL_EDITOR_HIGHLIGHT.md'):
        shutil.copy2(ROOT / name, prefix / name)
    env['VDK_HOME'] = prefix.as_posix()
    info = str(prefix / executables['vdkInfo'])
    subprocess.run([info, 'doctor'], env=env, check=True)
    assert subprocess.check_output([info, 'version'], env=env, encoding='utf-8').splitlines()[0] == f'VDK {version}'
    catalog = subprocess.check_output([info, 'library', 'list', '--json'], env=env)
    if json.loads(catalog) != json.loads((home / 'vpp-libraries.json').read_bytes()):
        raise ValueError('VDK catalog mismatch')
    return version


def check_sdk(prefix, platform, version):
    """Exercise the real launcher from a clean project, including SDK selection."""
    windows = platform == 'windows-x64'
    env = os.environ.copy()
    for key in list(env):
        if key.startswith('VPP_AOT_') and key != 'VPP_AOT_ASSEMBLER':
            env.pop(key)
        elif key in ('VDK_HOME', 'VPP_HOME', 'VPP_WINDOWS_RUNTIME_OBJECT', 'VPP_NATIVE_TARGET'):
            env.pop(key)
    if windows:
        env['VPP_AOT_ASSEMBLER'] = env.get('VPP_CI_CLANG', env.get('VPP_AOT_ASSEMBLER', 'clang.exe'))
        env['PATH'] = str(prefix / 'bin') + os.pathsep + env['PATH']
        launcher = [env['COMSPEC'], '/d', '/c', 'vdk.cmd']
    else:
        launcher = [str(prefix / 'bin/vdk')]

    with tempfile.TemporaryDirectory(prefix='vdk smoke ') as directory:
        work = Path(directory)
        home = work / 'home'
        home.mkdir()
        env['HOME'] = str(home)
        env['LOCALAPPDATA'] = str(home)
        env['VDK_DISPLAY_PROFILE_ROOT'] = str(home)
        env['SHELL'] = '/bin/bash'
        env['ZDOTDIR'] = str(home)
        if not windows:
            env['LC_ALL'] = 'C'
        project = work / 'project'
        nested = project / 'src'
        nested.mkdir(parents=True)

        def run(*args, cwd=nested, overrides=None, ok=True, expected_exit=None):
            result = subprocess.run([*launcher, *map(str, args)], cwd=cwd,
                                    env=dict(env, **(overrides or {})), capture_output=True,
                                    encoding='utf-8', errors='strict', timeout=300)
            if (result.returncode == 0) != ok or (expected_exit is not None and result.returncode != expected_exit):
                raise RuntimeError(f'VDK {args}: exit {result.returncode}\n{result.stdout}\n{result.stderr}')
            return result.stdout

        def expect_version(expected, *args, **kwargs):
            output = run(*args, **kwargs)
            if not output.startswith(f'VDK {expected}\n'):
                raise ValueError(f'Unexpected VDK version: {output}')
            return output

        expect_version(version, 'version')
        expect_version(version, 'phiên', 'bản')
        expect_version(version, 'phiên bản')
        assert 'ĐẠT' in run('chẩn', 'đoán')
        assert 'cài hiển thị' in run('trợ', 'giúp')
        run()
        expect_version(version, '--thư-mục-vdk', prefix, 'phiên bản')
        # Never change the actual user's profiles. Exercise repeat installation
        # and removal with unrelated Unicode configuration already present.
        if windows:
            profiles = [home / 'WindowsPowerShell/profile.ps1', home / 'PowerShell/profile.ps1']
            original = '# cấu hình riêng\n'.encode('utf-8-sig')
        else:
            profiles = [home / '.bashrc', home / '.profile']
            original = '# cấu hình riêng\n'.encode('utf-8')
        for profile in profiles:
            profile.parent.mkdir(parents=True, exist_ok=True)
            profile.write_bytes(original)
        run('cài', 'hiển', 'thị')
        installed = [profile.read_bytes() for profile in profiles]
        for content in installed:
            assert content.count(b'# >>> VDK UTF-8 >>>') == 1
        run('install-display')
        assert installed == [profile.read_bytes() for profile in profiles]
        assert 'Đắk Lắk' in run('kiểm', 'tra', 'hiển', 'thị')
        run('gỡ hiển thị')
        assert [profile.read_bytes() for profile in profiles] == [original] * len(profiles)
        run('cài hiển thị', '--invalid', ok=False)
        catalog = json.loads((prefix / 'share/vpp/vpp-libraries.json').read_text(encoding='utf-8'))
        if json.loads(run('thư', 'viện', 'danh', 'sách', '--json')) != catalog:
            raise ValueError('VDK Vietnamese catalog mismatch')
        ids = set(run('library', 'list').splitlines())
        expected_ids = {item['id'] for item in catalog['libraries']}
        expected_ids.update(group['name'] for group in catalog['moduleGroups'])
        if not expected_ids <= ids:
            raise ValueError(f'VDK catalog missing {expected_ids - ids}')
        run('library', 'list', '--invalid', ok=False)
        run('--vdk-home', ok=False)
        expect_version(version, '--vdk-home', prefix, 'version')
        expect_version(version, 'version', overrides={'VPP_HOME': str(prefix / 'share/vpp')})

        pin = project / 'vdk.json'
        other_version = version + '-test'
        store = home / ('VPP/vdk' if windows else '.vpp/vdk')
        other = store / other_version
        other.mkdir(parents=True)
        manifest = json.loads((prefix / 'vdk-release.json').read_text(encoding='utf-8'))
        manifest['version'] = other_version
        (other / 'vdk-release.json').write_text(json.dumps(manifest), encoding='utf-8')
        pin.write_text(json.dumps({'version': other_version}), encoding='utf-8')
        expect_version(other_version, 'version', overrides={'VDK_HOME': str(prefix)})
        run('--vdk-home', prefix, 'version', ok=False)
        run('doctor', ok=False)
        # Nearest pin wins; malformed and traversal-like pins must fail closed.
        (nested / 'vdk.json').write_text(json.dumps({'version': version}), encoding='utf-8')
        expect_version(version, 'version')
        (nested / 'vdk.json').unlink()
        for invalid in ('{', '{}', '{"version": 1}', '{"version": "../bad"}', '{"version": "missing-sdk"}'):
            pin.write_text(invalid, encoding='utf-8')
            run('version', ok=False)
        pin.unlink()
        expect_version(other_version, 'version', overrides={'VDK_HOME': str(other)})
        expect_version(version, '--vdk-home', prefix, 'version', overrides={'VDK_HOME': str(other)})

        source = nested / 'thử VDK.vi'
        source.write_text('hàm chính() { in 42; };\n', encoding='utf-8')
        output = nested / ('probe.exe' if windows else 'probe')
        run('--thư-mục-vdk', prefix, 'biên', 'dịch', source, '--đầu-ra', output)
        if subprocess.check_output([str(output)], cwd=nested, env=env).strip() != b'42':
            raise ValueError('VDK forwarded build output mismatch')
        if run('chạy', source).strip() != '42':
            raise ValueError('VDK Vietnamese AOT run output mismatch')
        if run('chạy', 'tức', 'thời', source).strip() != '42':
            raise ValueError('VDK Vietnamese JIT output mismatch')
        run('kiểm', 'tra', source)
        bytecode = nested / 'thử VDK.vmb'
        run('biên', 'dịch', 'máy', 'ảo', source, '--đầu-ra', bytecode)
        if subprocess.check_output([str(prefix / layout(platform)['vm']), str(bytecode)], cwd=nested, env=env).strip() != b'42':
            raise ValueError('VDK VM output mismatch')
        if run('chạy', '--artifact', bytecode).strip() != '42':
            raise ValueError('VDK artifact execution mismatch')
        if run('chạy', '--compiler=vpp', '--artifact', bytecode).strip() != '42':
            raise ValueError('VDK artifact compiler alias mismatch')
        if run('chạy', '--compiler=vpp', source).strip() != '42':
            raise ValueError('VDK source compiler alias mismatch')
        legacy = nested / 'old.vbc'
        legacy.write_bytes(bytecode.read_bytes())
        run('chạy', '--artifact', legacy, ok=False)
        run('chạy', '--artifact', nested / 'missing.vmb', ok=False)
        unicode_text = 'Tiếng Việt — Đắk Lắk'
        source.write_text(f'hàm chính() {{ in "{unicode_text}"; }};\n', encoding='utf-8')
        if run('chạy', source).strip() != unicode_text:
            raise ValueError('VDK Unicode AOT output mismatch')
        source.write_text('hàm chính() { in 42; };\n', encoding='utf-8')
        run('build', source, '-o', output)
        if run('jit', source).strip() != '42':
            raise ValueError('VDK forwarded JIT output mismatch')
        run('build', nested / 'missing.vi', '-o', output, ok=False)
        source.write_text('hàm chính() { trả về 7; };\n', encoding='utf-8')
        run('chạy', source, ok=False, expected_exit=7)
    print('VDK_LAUNCHER_REGRESSION=PASS', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=PLATFORMS, required=True)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prefix, output = args.prefix.resolve(strict=True), args.output.resolve()
    if output == prefix or prefix in output.parents:
        parser.error('output must be outside the installation prefix')
    output.mkdir(parents=True, exist_ok=True)
    _, _, vpp_name = PLATFORMS[args.platform]
    install_uninstaller(prefix, args.platform)
    check_uninstaller(prefix, args.platform)
    archive(prefix, output / vpp_name, args.platform, False)
    with tempfile.TemporaryDirectory(prefix='vdk release ') as work:
        sdk = Path(work) / 'SDK thử nghiệm'
        shutil.copytree(prefix, sdk, ignore=shutil.ignore_patterns('.vpp', '.git', '__pycache__', '.DS_Store', '._*'))
        version = build_sdk(sdk, args.platform)
        install_uninstaller(sdk, args.platform)
        check_sdk(sdk, args.platform, version)
        check_uninstaller(sdk, args.platform, sdk=True)
        extension = '.zip' if args.platform == 'windows-x64' else '.tar.gz'
        archive(sdk, output / f'vdk-{version}-{args.platform}{extension}', args.platform, True)


if __name__ == '__main__':
    main()
