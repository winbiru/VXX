"""Check the extracted release using Windows PowerShell 5.1 and no user PATH edits."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import time


def imported_dlls(path):
    """Read PE imports directly: CI must catch CRT dependencies even if installed."""
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    assert data[pe:pe + 4] == b'PE\0\0', 'Not a PE executable'
    sections, = struct.unpack_from('<H', data, pe + 6)
    optional_size, = struct.unpack_from('<H', data, pe + 20)
    optional = pe + 24
    magic, = struct.unpack_from('<H', data, optional)
    assert magic == 0x20B, 'Expected Windows x64 PE32+'
    table = optional + optional_size

    def offset(rva):
        for index in range(sections):
            section = table + index * 40
            size, address, raw_size, raw = struct.unpack_from('<IIII', data, section + 8)
            if address <= rva < address + max(size, raw_size):
                return raw + rva - address
        raise AssertionError(f'Unmapped PE RVA: {rva}')

    imports, = struct.unpack_from('<I', data, optional + 112 + 8)
    if not imports:
        return []
    cursor = offset(imports)
    names = []
    while any(data[cursor:cursor + 20]):
        name_rva, = struct.unpack_from('<I', data, cursor + 12)
        start = offset(name_rva)
        names.append(data[start:data.index(b'\0', start)].decode('ascii').lower())
        cursor += 20
    return names


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    args = parser.parse_args()
    assert os.name == 'nt', 'Run this check on Windows'
    assert (args.archive / 'install-vpp.ps1').read_bytes().startswith(b'\xef\xbb\xbf'), \
        'Windows PowerShell 5.1 requires UTF-8 BOM for the Vietnamese installer'
    dependencies = imported_dlls(args.archive / 'vpp.exe')
    forbidden = [name for name in dependencies if name.startswith(
        ('vcruntime', 'msvcp', 'ucrtbase', 'api-ms-win-crt', 'vpp-'))]
    assert not forbidden, f'Release needs unbundled runtime DLLs: {forbidden}'

    # Restricted parent policy reproduces the user's default PowerShell setup.
    # The .cmd launcher must bypass it only in its child installer process.
    launcher = str((args.archive / 'install-vpp.cmd').resolve()).replace("'", "''")
    destination = str(args.install.resolve()).replace("'", "''")
    command = f"& '{launcher}' -InstallDir '{destination}' -NoPathUpdate; exit $LASTEXITCODE"
    subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Restricted',
                    '-Command', command], check=True)
    assert (args.install / 'gói').is_dir(), 'Vietnamese stdlib directory missing'
    # Direct invocation must update the calling PowerShell session immediately.
    # NoPathUpdate lets CI test this without altering the runner's user registry.
    installer = str((args.archive / 'install-vpp.ps1').resolve()).replace("'", "''")
    environment_check = r"""
$ErrorActionPreference = 'Stop'
$savedPath = [Environment]::GetEnvironmentVariable('Path', 'User')
$savedHome = [Environment]::GetEnvironmentVariable('VPP_HOME', 'User')
$destination = '__DESTINATION__'
[Console]::OutputEncoding = [System.Text.Encoding]::GetEncoding(437)
$global:OutputEncoding = [System.Text.Encoding]::ASCII
$env:Path = $env:Path + ';' + $destination + '\;' + $destination.ToUpperInvariant()
1..2 | ForEach-Object {
    & '__INSTALLER__' -InstallDir $destination -NoPathUpdate
    if ([Console]::OutputEncoding.CodePage -ne 65001 -or $OutputEncoding.CodePage -ne 65001) {
        throw 'Installer did not configure UTF-8 in the calling session'
    }
    if ($env:VPP_HOME -ne $destination) { throw 'VPP_HOME missing from current session' }
    if (($env:Path -split ';')[0] -ne $destination) { throw 'VPP not first in PATH' }
    $matches = @($env:Path -split ';' | Where-Object {
        $_.TrimEnd([char[]]'\/') -ieq $destination
    })
    if ($matches.Count -ne 1) { throw 'Duplicate VPP PATH entries' }
    if ((Get-Command vpp -CommandType Application).Source -ne (Join-Path $destination 'vpp.exe')) {
        throw 'vpp resolves to the wrong installation'
    }
    vpp
    if ($LASTEXITCODE -ne 0) { throw 'vpp cannot run in current session' }
}
if ([Environment]::GetEnvironmentVariable('Path', 'User') -ne $savedPath -or
    [Environment]::GetEnvironmentVariable('VPP_HOME', 'User') -ne $savedHome) {
    throw 'NoPathUpdate modified persistent user environment'
}
""".replace('__DESTINATION__', destination).replace('__INSTALLER__', installer)
    subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                    '-Command', environment_check], cwd=args.install, check=True)
    for arguments, expected in [(['phiên', 'bản'], 'Phiên bản'),
                                (['giúp', 'đỡ'], 'Cách dùng'),
                                ([], 'V++ CLI')]:
        result = subprocess.run([str(args.install.resolve() / 'vpp.exe'), *arguments],
                                cwd=args.install, capture_output=True, check=True)
        assert expected in result.stdout.decode('utf-8'), result.stdout
    # Use a separate installation so the remaining release checks can still run.
    uninstall_dir = args.install.resolve().with_name('uninstall smoke')
    subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                    '-File', str(args.archive.resolve() / 'install-vpp.ps1'),
                    '-InstallDir', str(uninstall_dir), '-NoPathUpdate'], check=True)
    user_file = uninstall_dir / 'my-project.vi'
    user_file.write_text('in 42;', encoding='utf-8')
    subprocess.run([str(uninstall_dir / 'vpp.exe'), 'gỡ', 'cài', 'đặt'], check=True)
    managed = ('vpp.exe', 'gói', 'templates', 'examples', 'uninstall-vpp.ps1', 'vpp-uninstall.cmd')
    deadline = time.monotonic() + 30
    while any((uninstall_dir / name).exists() for name in managed) and time.monotonic() < deadline:
        time.sleep(0.1)
    assert user_file.read_text(encoding='utf-8') == 'in 42;', 'Uninstall deleted user project'
    for name in managed:
        assert not (uninstall_dir / name).exists(), f'Uninstall left {name}'
    print('Windows install/uninstall, static runtime and UTF-8 CLI smoke checks passed')


if __name__ == '__main__':
    main()
