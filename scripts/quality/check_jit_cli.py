"""Check standalone native JIT without a toolchain or generated files."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    executable = Path(sys.argv[1]).resolve(strict=True)
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith('VPP_')}
    env.update(PATH=str(Path(os.environ['SystemRoot']) / 'System32') if os.name == 'nt' else '',
               VPP_AOT_ASSEMBLER='/assembler-must-not-run',
               VPP_AOT_LINKER='/linker-must-not-run')
    with tempfile.TemporaryDirectory(prefix='vpp-jit-') as work:
        root = Path(work)
        source = root / 'thử JIT.vi'
        source.write_text('hàm cộng(a,b) { trả về a+b; }; hàm chính() { in cộng(19,23); };', encoding='utf-8')
        bad = root / 'bad.vi'
        bad.write_text('hàm chính( {', encoding='utf-8')
        unsupported = root / 'unsupported.vi'
        unsupported.write_text('hàm chính() { in "abc"; };', encoding='utf-8')
        before = {p.name: p.read_bytes() for p in root.iterdir()}
        cases = [([source], 0, b'42\n'),
                 (['jit', source], 0, b'42\n'),
                 ([source, '--function', 'cộng', '--', '19', '23'], 42, b''),
                 ([], 2, None), (['--help'], 0, None),
                 ([bad], 1, None), ([unsupported], 1, None),
                 ([source, '--function', 'missing'], 1, None),
                 ([source, '--function', 'cộng', '--', 'abc', '23'], 1, None)]
        for args, status, expected in cases:
            result = subprocess.run([str(executable), *map(str, args)], cwd=root, env=env,
                                    capture_output=True, timeout=30)
            output = result.stdout.replace(b'\r\n', b'\n')
            if result.returncode != status or (expected is not None and output != expected):
                raise RuntimeError(f'{args}: exit {result.returncode}, stdout={output!r}, stderr={result.stderr!r}')
            if (not args or args == ['--help']) and b'Usage:' not in output:
                raise RuntimeError(f'{args}: missing usage text')
        if {p.name: p.read_bytes() for p in root.iterdir()} != before:
            raise RuntimeError('JIT changed or created files in its working directory')
    print('V++ JIT CLI PASS')


if __name__ == '__main__':
    main()
