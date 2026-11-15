#!/usr/bin/env python3
"""Process doubles for the JIT regression-runner self-test.

Only the self-test uses this protocol. Published files are copies of its
prepared JIT host, allowing staging/hash assertions without building anything.
The V++ self-test still owns all scenarios and result assertions.
"""
import http.server
import os
from pathlib import Path
import subprocess
import sys
import time


def main():
    logical_self = Path(sys.argv[1])
    args = [str(logical_self), *sys.argv[2:]]
    mode = os.environ['VPP_TEST_MODE']
    state = Path(os.environ['VPP_TEST_STATE'])

    def mark(name):
        (state / name).write_text('1')

    def publish(destination, label):
        # Preserve the executable-copy protocol, including CMake failures.
        subprocess.run([os.environ['VPP_TEST_CMAKE'], '-E', 'copy', str(logical_self), destination], check=True)
        Path(destination + '.label').write_text(label)

    name = logical_self.stem
    if name == 'server':
        mark('server-started')

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                body = b'{"ok":true}'
                self.send_response(200)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *args):
                pass

        class Server(http.server.HTTPServer):
            allow_reuse_address = True

        with Server(('127.0.0.1', 18080), Handler) as server:
            server.serve_forever()
    elif name in ('vpp', 'vpp-native', 'vpp-i32'):
        op = args[1]
        if op == 'batch-build':
            assert os.environ['VPP_NATIVE_NO_CACHE'] == '0'
            profile = Path(os.environ['VPP_COMPILER_PROFILE_FILE'])
            assert profile.name == 'compiler.profile'
            assert profile != state / 'profile'
            profile.write_text('batch profile')
            for i in range(2, len(args), 2):
                source = Path(args[i]).name
                mark('build-' + source)
                if mode != 'missing-output' or i != 2:
                    publish(args[i + 1], source)
            if mode in ('out-of-order', 'nan', 'malformed'):
                print({'out-of-order': 'VPP_BATCH_BUILD 3 10', 'nan': 'VPP_BATCH_BUILD 1 NaN',
                       'malformed': 'VPP_BATCH_BUILD 1'}[mode], flush=True)
                return 0
            print('VPP_BATCH_BUILD 1 12.5', flush=True)
            if mode == 'timeout':
                time.sleep(5)
                return 0
            if mode in ('partial', 'missing-marker'):
                return int(mode == 'partial')
            print('VPP_BATCH_BUILD 3 2e3', flush=True)
            return int(mode == 'late-failure')
        assert op in ('build', 'object'), 'unexpected fake compiler command'
        source, destination = Path(args[2]), args[4]
        mark('build-' + source.name)
        if (source.name in ('reject.vi', 'broken.vi') or
                (source.name == 'chính.vi' and mode == 'vm-fail') or
                (source.name == 'http_fixture.vi' and mode == 'http-fail')):
            return 1
        if source.name == 'suite.vi':
            assert 'ca2.kiểm thử()' in source.read_text()
            assert (source.parent / 'case2.vi').is_file()
            if mode == 'module-fail':
                print('module diagnostic')
                return 1
        publish(destination, source.name)
        return 0
    else:
        label = Path(str(logical_self) + '.label').read_text()
        if label == 'tooling.vi':
            own = Path(os.environ['VPP_TEST_SELF'])
            assert own.name == logical_self.name and own.is_file()
            (Path(os.environ['VPP_TEST_WORK']) / 'tooling-marker').write_text('ok')
            mark('tooling-ran')
        if mode in ('barrier', 'batch-groups'):
            names = ('first', 'second', 'reject', 'broken') if mode == 'barrier' else ('first', 'second', 'reject', 'third', 'fourth')
            assert all((state / ('build-' + name + '.vi')).is_file() for name in names), 'build barrier'
        if label == 'suite.vi':
            selector = args[1]
            (state / ('run-module-' + selector)).write_text(os.getcwd())
            if selector == '1':
                print('wrong')
                return 0
        else:
            mark('run-' + label)
        if label in ('http1.vi', 'http2.vi'):
            lock = state / 'http-active'
            assert not lock.exists(), 'HTTP clients overlapped'
            lock.write_text('active')
            time.sleep(.15)
            lock.unlink()
            assert (state / 'server-started').is_file()
        print('ok')
        return 0


if __name__ == '__main__':
    raise SystemExit(main())
