#!/usr/bin/env python3
"""Exercise native LSP catalog compatibility over raw JSON-RPC on every OS."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def packet(method, request_id=None, params=None):
    msg = {'jsonrpc': '2.0', 'method': method}
    if request_id is not None:
        msg['id'] = request_id
    if params is not None:
        msg['params'] = params
    body = json.dumps(msg, ensure_ascii=False).encode('utf-8')
    return b'Content-Length: %d\r\n\r\n' % len(body) + body


def decode(wire):
    result = []
    while wire:
        header, wire = wire.split(b'\r\n\r\n', 1)
        assert header.startswith(b'Content-Length: '), header
        n = int(header.split(b': ')[1])
        assert len(wire) >= n
        result.append(json.loads(wire[:n]))
        wire = wire[n:]
    return result


def revision(raw, version):
    h = 1469598103934665603
    for b in raw:
        h = ((h ^ b) * 1099511628211) & ((1 << 64) - 1)
    return f'{version}:{h:x}'


def check(tools, compiler, root):
    with tempfile.TemporaryDirectory(prefix='vpp lsp ') as work:
        workspace = Path(work) / 'dự án 🌿'
        workspace.mkdir()
        catalog = workspace / 'vpp-libraries.json'
        # Force a response through multiple 128-byte output chunks, including
        # a UTF-8 sequence split across a chunk boundary.
        entry = dict(id='vi.test', displayName='Thử nghiệm ' + '🌿a' * 130, version='1.0', stability='stable',
                     kind='library', sourceRoot='gói/thử nghiệm', entry='chính.vi')
        raw = json.dumps(dict(schema=1, sdkVersion='test', moduleGroups=[{'libraries':['vi.test']}], libraries=[entry]), ensure_ascii=False).encode('utf-8')
        catalog.write_bytes(raw)
        env = dict(os.environ, VPP_HOME=str(root))
        wire = b''.join((packet('initialize', 1, {'rootUri': workspace.as_uri()}),
                         packet('vpp/libraryRoots', 2),
                         packet('workspace/didChangeWatchedFiles', params={'changes':[{'uri':catalog.as_uri(),'type':2}]}),
                         packet('shutdown', 3), packet('exit')))
        result = subprocess.run([str(tools), str(compiler), '--lsp'], input=wire, capture_output=True, env=env, timeout=120)
        assert result.returncode == 0, result.stderr.decode('utf-8', errors='replace')
        assert not result.stderr, result.stderr
        # CMake covers file redirection. Also exercise the packaged Windows
        # launcher with a pipe, as used by editor language clients.
        if os.name == 'nt':
            launcher = tools.with_name('vpp.cmd')
            assert launcher.is_file(), launcher
            for alias in ('--lsp', 'lsp'):
                launched = subprocess.run([os.environ['COMSPEC'], '/d', '/c', 'vpp.cmd', alias],
                                          cwd=launcher.parent, input=wire, capture_output=True,
                                          env=env, timeout=120)
                assert launched.returncode == 0, (alias, launched.returncode, launched.stderr)
                assert not launched.stderr, (alias, launched.stderr)
                assert launched.stdout == result.stdout, (alias, launched.stdout, result.stdout)
            print('Native LSP Windows launcher: pipe, both aliases and raw response parity PASS')
        # An absent or truncated stream must not fail with an empty diagnostic.
        for incomplete, diagnostic in ((b'', 'LSP: đầu vào đã kết thúc'),
                                       (b'Content-Length: 10\r\n', 'Tiêu đề LSP bị cắt'),
                                       (b'Content-Length: 10\r\n\r\n{}', 'Gói LSP bị cắt')):
            broken = subprocess.run([str(tools), str(compiler), '--lsp'], input=incomplete,
                                    capture_output=True, env=env, timeout=120)
            assert broken.returncode == 1 and not broken.stdout, broken
            assert diagnostic.encode('utf-8') in broken.stderr, broken.stderr
        messages = decode(result.stdout)
        assert len(messages) == 4, messages
        assert messages[0]['result']['capabilities']['documentFormattingProvider'] is True, messages[0]
        expected = dict(revision=revision(raw, 'test'), catalogUri=catalog.as_uri(), diagnostics=[], libraries=[dict(entry, rootUri=(workspace / entry['sourceRoot']).as_uri(), entryUri=(workspace / entry['sourceRoot'] / entry['entry']).as_uri())])
        assert messages[1]['result'] == expected, messages[1]
        assert messages[2]['method'] == 'vpp/libraryRootsChanged' and messages[2]['params'] == expected, messages[2]
        assert messages[3]['result'] is None
        # A malformed local catalog must produce a diagnostic, not terminate the server.
        catalog.write_bytes(b'{')
        result = subprocess.run([str(tools), str(compiler), '--lsp'], input=wire, capture_output=True, env=env, timeout=120)
        assert result.returncode == 0, result.stderr
        messages = decode(result.stdout)
        assert messages[1]['result']['diagnostics'][0]['severity'] == 'error', messages[1]
        assert messages[1]['result']['libraries'] == []
        # With no workspace catalog, discover the actual installed SDK catalog.
        catalog.unlink()
        result = subprocess.run([str(tools), str(compiler), '--lsp'], input=wire, capture_output=True, env=env, timeout=120)
        assert result.returncode == 0, result.stderr
        messages = decode(result.stdout)
        raw = (root / 'vpp-libraries.json').read_bytes()
        data = json.loads(raw)
        assert messages[1]['result']['revision'] == revision(raw, data['sdkVersion']), messages[1]
        assert len(messages[1]['result']['libraries']) == len(data['libraries'])
    print('Native LSP catalog: workspace precedence, UTF-8 URI, revision, changes, malformed catalog and SDK fallback PASS')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('tools', type=Path)
    parser.add_argument('compiler', type=Path)
    parser.add_argument('root', type=Path)
    args = parser.parse_args()
    check(args.tools.resolve(), args.compiler.resolve(), args.root.resolve())
