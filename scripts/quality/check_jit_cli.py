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
        script = root / 'script.vi'
        script.write_text('in "thông dịch"; lặp(i = 1; i <= 3; i++) { in i; };', encoding='utf-8')
        functions = root / 'functions.vi'
        functions.write_text('hàm đệ_quy(n) { nếu (n <= 1) { trả về 1; }; trả về n * đệ_quy(n - 1); };'
                             ' hàm chính() { in đệ_quy(5); };', encoding='utf-8')
        methods = root / 'methods.vi'
        methods.write_text('lớp công khai Hộp { hàm công khai khởi tạo() { mình.đầu = 9; };'
                           ' hàm công khai đầu() { trả về 42; }; };'
                           ' hàm chính() { a = Hộp(); in a.đầu(); in a.đầu; };', encoding='utf-8')
        class_entrypoint = root / 'class-entrypoint.vi'
        class_entrypoint.write_text('lớp Chính { hàm khởi tạo() { mình.giá_trị = 101; };'
                                    ' hàm công khai chạy() { in mình.giá_trị; }; };', encoding='utf-8')
        increment = root / 'increment.vi'
        increment.write_text('hàm chính() { x = 1.5; x++; in x; x--; in x; };', encoding='utf-8')
        intrinsics = root / 'intrinsics.vi'
        intrinsics.write_text('in bộ([7, 8]); in máy ảo lấy độ dài(ten nền tảng máy ảo()) > 0;',
                              encoding='utf-8')
        byte_intrinsics = root / 'byte-intrinsics.vi'
        byte_intrinsics.write_text('in chuỗi từ byte máy ảo(chuỗi thành byte máy ảo("chào"));'
                                   ' in máy ảo cắt chuỗi("abcdef", 2, 3);'
                                   ' in số thực từ bit máy ảo([0,0,0,0,0,0,240,63]) == 1;'
                                   ' in máy ảo lấy độ dài(máy nguồn ngẫu nhiên các byte(8));', encoding='utf-8')
        intrinsic_shadow = root / 'intrinsic-shadow.vi'
        intrinsic_shadow.write_text('hàm chính() { bộ = [1, 2]; in bộ([7, 8]); in bộ; };', encoding='utf-8')
        before = {p.name: p.read_bytes() for p in root.iterdir()}
        cases = [([source], 0, b'42\n'),
                 (['jit', source], 0, b'42\n'),
                 (['--interpret', source], 0, b'42\n'),
                 (['jit', '--interpret', source], 0, b'42\n'),
                 (['--interpret', script], 0, 'thông dịch\n1\n2\n3\n'.encode()),
                 (['--interpret', functions], 0, b'120\n'),
                 (['--interpret', methods], 0, b'42\n9\n'),
                 (['--interpret', class_entrypoint], 0, b'101\n'),
                 (['--interpret', increment], 0, b'2\n1\n'),
                 (['--interpret', intrinsics], 0, b'(7, 8)\n1\n'),
                 (['--interpret', byte_intrinsics], 0, 'chào\ncde\n1\n8\n'.encode()),
                 (['--interpret', intrinsic_shadow], 0, b'(7, 8)\n[1, 2]\n'),
                 (['--interpret', bad], 1, None),
                 (['--interpret'], 1, None),
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
