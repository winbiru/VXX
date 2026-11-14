"""Execute dynamic native JIT with no external toolchain, including hot guards."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / 'scripts/test/fixtures'
EXPECTED = ROOT / 'scripts/test/expected'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('vpp_home', type=Path)
    parser.add_argument('--baseline-only', action='store_true')
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    home = args.vpp_home.resolve(strict=True)
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith('VPP_')}
    env.update(PATH=str(Path(os.environ['SystemRoot']) / 'System32') if os.name == 'nt' else '',
               VPP_HOME=str(home), VPP_AOT_ASSEMBLER='/assembler-must-not-run',
               VPP_AOT_LINKER='/linker-must-not-run')
    with tempfile.TemporaryDirectory(prefix='vpp-jit-dynamic-') as work:
        work = Path(work)
        minimal = work / 'thử động.vi'
        minimal.write_text('hàm chính() { in "Tiếng Việt"; trả về 7; };', encoding='utf-8')
        bad = work / 'bad.vi'
        bad.write_text('hàm chính( {', encoding='utf-8')
        library = work / 'thư viện.vi'
        library.write_text('nhập gói.lõi.toán học.*;\nhàm chính() { in bình phương(6); in là chẵn(8); in là chẵn(7); in tổng hai(10,5); };', encoding='utf-8')
        argv = work / 'đối số.vi'
        argv.write_text('''hàm đối số văn bản(i) {
    h = đối số lấy(i);
    bytes = [];
    lặp(j = 0; j < byte dài(h); j++) { máy ảo thêm cuối danh sách(bytes, byte lấy(h, j)); };
    byte hủy(h);
    trả về chuỗi từ byte máy ảo(bytes);
};
hàm chính() {
    in đối số đếm();
    lặp(i = 0; i < đối số đếm(); i++) { in đối số văn bản(i); };
    in đối số văn bản(đối số đếm());
    in đối số văn bản(-1);
};''', encoding='utf-8')
        missing = work / 'missing.vi'
        missing.write_text('hàm chính() { trả về hàm_không_tồn_tại(); };', encoding='utf-8')
        before = {p.name: p.read_bytes() for p in work.iterdir()}

        def run(source, status, expected=None, profile=False, tier=True, *, mode="--dynamic", guest_args=(), diagnostic=None, timeout=600):
            run_env = dict(env)
            if profile:
                run_env['VPP_JIT_PROFILE'] = '1'
            if not tier:
                run_env['VPP_JIT_TIER'] = '0'
            result = subprocess.run([str(executable), mode, str(source), *guest_args], cwd=work,
                                    env=run_env, capture_output=True, timeout=timeout)
            output = result.stdout.replace(b'\r\n', b'\n')
            error = result.stderr.replace(b'\r\n', b'\n')
            if result.returncode != status or (expected is not None and output != expected):
                raise RuntimeError(f'{source}: exit {result.returncode}, stdout={output!r}, stderr={error!r}')
            if diagnostic is not None and diagnostic not in output + error:
                raise RuntimeError(f'{source}: missing diagnostic {diagnostic!r}; stdout={output!r}, stderr={error!r}')
            if status in (0, 7) and not profile and error:
                raise RuntimeError(f'{source}: unexpected stderr {error!r}')
            print(f'PASS dynamic {source.name}', flush=True)
            return error

        run(minimal, 7, 'Tiếng Việt\n'.encode())
        run(bad, 1)
        run(minimal, 0, b'', mode='--check-dynamic')
        run(bad, 1, mode='--check-dynamic', diagnostic='V++ JIT: từ chối cú pháp'.encode())
        run(missing, 1, mode='--check-dynamic', diagnostic='hàm_không_tồn_tại'.encode())
        run(argv, 0, f'1\n{argv}\n\n\n'.encode())
        run(argv, 0, f'4\n{argv}\nTiếng Việt\n\n--dynamic\n\n\n'.encode(),
            guest_args=('Tiếng Việt', '', '--dynamic'))
        for stem in ('native_dynamic_vpp', 'native_aot_private_method'):
            run(FIXTURES / f'{stem}.vi', 0, (EXPECTED / f'{stem}.expected').read_bytes().replace(b'\r\n', b'\n'))
        # Exercise both the narrow library resolver and the full umbrella.
        run(library, 0, b'36\n1\n0\n15\n')
        compound = ROOT / 'scripts/test/kiem_tra_gan_ket_hop.vi'
        run(compound, 0, (EXPECTED / 'kiem_tra_gan_ket_hop.expected').read_bytes().replace(b'\r\n', b'\n'))
        if not args.baseline_only:
            large = ROOT / 'scripts/test/kiem_tra_aot_type_branch.vi'
            run(large, 0, (EXPECTED / 'kiem_tra_aot_type_branch.expected').read_bytes().replace(b'\r\n', b'\n'), timeout=1800)
            profile = run(FIXTURES / 'native_jit_hot.vi', 0,
                          (EXPECTED / 'native_jit_hot.expected').read_bytes().replace(b'\r\n', b'\n'), True)
            rows = {}
            for line in profile.decode('utf-8').splitlines():
                match = re.fullmatch(r'VPP_JIT_PROFILE fn=\d+ name=(.+) calls=(\d+) i32=(\d+) compiled=(\d+) hits=(\d+)', line)
                if not match:
                    raise RuntimeError(f'unexpected profile line: {line!r}')
                rows[match[1]] = tuple(map(int, match.groups()[1:]))
            for name, expected in {'hot_add': (44, 42, 1, 11),
                                   **{f'hot_{op}': (40, 40, 1, 9) for op in ('sub', 'mul', 'and', 'or', 'xor')},
                                   'cold_add': (1, 1, 0, 0),
                                   'generic_add': (40, 0, 0, 0)}.items():
                matches = [v for k, v in rows.items() if k == name or k.endswith('.' + name)]
                if matches != [expected]:
                    raise RuntimeError(f'{name}: expected {expected}, actual {matches}; profile={rows}')
            print('PASS adaptive native recompilation and type guards', flush=True)
            disabled = run(FIXTURES / 'native_jit_hot.vi', 0,
                           (EXPECTED / 'native_jit_hot.expected').read_bytes().replace(b'\r\n', b'\n'),
                           profile=True, tier=False)
            if disabled:
                raise RuntimeError(f'disabled tier emitted diagnostics: {disabled!r}')
            print('PASS tier disable preserves baseline output', flush=True)
            pressure = run(FIXTURES / 'native_jit_pressure.vi', 0, b'42\n', profile=True)
            # The VM reserves 60 of the 64 byte-resource slots for its heap.
            # All four public slots remain available after specialization.
            if not re.search(rb'name=(?:[^\n]*\.)?hot_pressure calls=41 i32=41 compiled=1 hits=10\n', pressure):
                raise RuntimeError(f'hot leaf did not survive GC and byte pressure: {pressure!r}')
            print('PASS adaptive leaf under GC and public byte-slot pressure', flush=True)
        if {p.name: p.read_bytes() for p in work.iterdir()} != before:
            raise RuntimeError('JIT changed or created files in its working directory')
    print('V++ DYNAMIC JIT PASS')


if __name__ == '__main__':
    # Bootstrap captures these streams as bytes and decodes the logs as UTF-8.
    # Redirected Windows stdout can otherwise default to a legacy code page.
    sys.stdout.reconfigure(encoding='utf-8')
    sys.stderr.reconfigure(encoding='utf-8', errors='backslashreplace')
    main()
