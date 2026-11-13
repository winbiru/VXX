#!/usr/bin/env python3
"""Full-language Darwin ARM64 executable selfhost, with a prepared runtime.

The optional native executable link route uses V++ instead of system ld.
The direct ARM64 route emits objects and signs executables in V++, using
prebuilt runtime and platform ABI objects.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import tempfile
import time

from check_native_macho_layout import (smoke as layout_smoke, patch_smoke, executable_smoke,
                                       verify_native_signature, parse as parse_macho_object)


def run(args, env=None, code=0, timeout=1800):
    started = time.monotonic()
    result = subprocess.run([str(a) for a in args], env=env, capture_output=True, timeout=timeout)
    if result.returncode != code:
        raise RuntimeError(f"{args}: exit {result.returncode}, expected {code}\n"
                           f"{result.stdout.decode(errors='replace')}\n"
                           f"{result.stderr.decode(errors='replace')}")
    return result, time.monotonic() - started


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sections(path):
    # Compare mapped sections independently of linker UUID and code signature.
    # Include VM addresses and flags so identical bytes at different addresses
    # cannot hide a layout change. Exclude only the non-file-backed payload.
    data = path.read_bytes()
    magic, cpu, _, kind, ncmd, size, _, _ = struct.unpack_from('<8I', data)
    assert (magic, cpu, kind) == (0xfeedfacf, 0x100000c, 2), path
    pos = 32
    result = {}
    for _ in range(ncmd):
        cmd, length = struct.unpack_from('<II', data, pos)
        assert length >= 8 and pos + length <= 32 + size
        if cmd == 0x19:  # LC_SEGMENT_64
            count = struct.unpack_from('<I', data, pos + 64)[0]
            assert 72 + 80 * count <= length
            for i in range(count):
                off = pos + 72 + 80 * i
                name, seg, addr, size_bytes, offset, align, _, _, flags, *_ = struct.unpack_from('<16s16sQQ8I', data, off)
                key = (seg.rstrip(b'\0'), name.rstrip(b'\0'))
                assert key not in result
                zero_fill = flags & 0xff in (1, 0xc, 0x12)
                assert zero_fill or offset + size_bytes <= len(data)
                result[key] = (addr, size_bytes, align, flags,
                               b'' if zero_fill else data[offset:offset + size_bytes])
        pos += length
    assert pos == 32 + size and (b'__TEXT', b'__text') in result
    return result


def audit(compiler):
    run(['/usr/bin/codesign', '--verify', compiler])
    libraries, _ = run(['/usr/bin/otool', '-L', compiler])
    names = [line.strip().split(b' (', 1)[0] for line in libraries.stdout.splitlines()[1:]]
    assert names == [b'/usr/lib/libSystem.B.dylib'], names
    symbols, _ = run(['/usr/bin/nm', '-g', compiler])
    assert not re.search(rb'__cxa_|__Z[0-9NSt]|ffi_|vm_step|linkedBytecodeRun|executeVpp|bytecode_dispatch', symbols.stdout)
    assert b'_vpp_fn_vm_collect' in symbols.stdout
    assert b'_vpp_fn_aot_integer' in symbols.stdout
    print(f'AUDIT PASS {compiler.name} sha256={digest(compiler)}', flush=True)


def verify_convergence(stage2, stage3):
    second, third = sections(stage2), sections(stage3)
    assert second.keys() == third.keys(), 'Mach-O section set differs'
    for key in second:
        assert second[key] == third[key], f'Mach-O section differs: {key}'
    print('FULL_NATIVE_EXECUTABLE_SECTION_CONVERGENCE=PASS', flush=True)
    second_hash, third_hash = digest(stage2), digest(stage3)
    if second_hash != third_hash:
        raise RuntimeError(f'FULL_NATIVE_EXECUTABLE_EXACT_CMP=FAIL (sections match): '
                           f'{second_hash} != {third_hash}')
    print('FULL_NATIVE_EXECUTABLE_EXACT_CMP=PASS', flush=True)


def verify_paged_object(path):
    parts, symbols = parse_macho_object(path)
    text = [(6004 * f + 4 * i, 0x2d000001)
            for f in range(3) for i in range(1500)]
    data = [(8 * i, 0x0e000001) for i in range(4500)]
    assert parts == [(0, 0, 18020, 4, text), (2, 18024, 36000, 8, data)], parts[:1]
    assert symbols == [('_caller0', 15, 0, 0), ('_callee', 15, 0, 18012),
                       ('_caller1', 15, 0, 6004), ('_caller2', 15, 0, 12008),
                       ('_table', 15, 1, 18024)], symbols
    print(f'PAGED OBJECT RELOCATIONS PASS count=9000 sha256={digest(path)}', flush=True)


def asm_stream_parity(compiler, root, out, env):
    # Capture the actual program.S passed to clang, without changing the
    # compiler's private-directory ownership or requiring PATH lookup.
    fixture = root / 'src/tests/fixtures/native_return_semantics.vi'
    for direct in ('1', '0'):
        captures = []
        executables = []
        for stream in ('0', '1'):
            tag = f'asm-direct{direct}-stream{stream}'
            capture = out / (tag + '.S')
            wrapper = out / (tag + '-clang')
            wrapper.write_text('#!/bin/sh\n'
                               + ('[ "${1-}" = -c ] || exit 99\n'
                                  if env.get('VPP_AOT_NATIVE_MACHO_LINK') == '1' else '')
                               +
                               'if [ "${1-}" = -c ] && [ "${2##*/}" = program.S ]; then\n'
                               f'  /bin/cp "$2" {shlex.quote(str(capture))} || exit 1\n'
                               'fi\nexec /usr/bin/clang "$@"\n')
            wrapper.chmod(0o700)
            target = out / tag
            _, elapsed = run([compiler, 'build', fixture, '-o', target],
                             {**env, 'VPP_AOT_ASSEMBLER': str(wrapper),
                              'VPP_AOT_ASM_STREAM': stream, 'VPP_AOT_ASM_DIRECT': direct})
            result, _ = run([target], env, code=42)
            expected = (root / 'src/tests/expected/native_return_semantics.expected').read_bytes()
            assert result.stdout == expected and not result.stderr
            assert capture.stat().st_size > 0
            captures.append(capture)
            executables.append(target)
            print(f'ASM direct={direct} stream={stream}: {elapsed:.3f}s', flush=True)
        assert captures[0].read_bytes() == captures[1].read_bytes(), f'ASM stream parity direct={direct}'
        assert sections(executables[0]) == sections(executables[1]), f'executable stream parity direct={direct}'
        print(f'ASM STREAM PARITY PASS direct={direct} sha256={digest(captures[0])}', flush=True)


def macho_reader_expected(path):
    """Independent struct oracle; keep signed words as the V++ reader does."""
    data = path.read_bytes()
    magic, cpu, subtype, kind, ncmd, _, _, _ = struct.unpack_from('<8I', data)
    assert (magic, cpu, subtype, kind) == (0xfeedfacf, 0x100000c, 0, 1)
    pos, parts, symtab = 32, [], None
    for _ in range(ncmd):
        cmd, length = struct.unpack_from('<II', data, pos)
        if cmd == 0x19:
            count = struct.unpack_from('<I', data, pos + 64)[0]
            for i in range(count):
                name, seg, address, size, offset, align, reloff, nrel, flags, *_ = struct.unpack_from(
                    '<16s16sQQ8I', data, pos + 72 + i * 80)
                parts.append(dict(name=name.rstrip(b'\0').decode(), segment=seg.rstrip(b'\0').decode(),
                                  address=address, size=size, offset=offset, align=align,
                                  relocationOffset=reloff, relocationCount=nrel,
                                  flags=struct.unpack('<i', struct.pack('<I', flags))[0],
                                  zeroFill=int(flags & 255 in (1, 12, 18))))
        elif cmd == 2:
            symtab = struct.unpack_from('<4I', data, pos + 8)
        pos += length
    assert symtab is not None
    symoff, nsyms, stroff, strsize = symtab
    expected = [[len(parts), nsyms], list(data[4090:4106])]
    for part in parts:
        expected.append(part)
        count = part['relocationCount']
        if count:
            for i in (0, count - 1):
                address, bits = struct.unpack_from('<iI', data, part['relocationOffset'] + i * 8)
                expected.append(dict(address=address, symbol=bits & 0xffffff, pcrel=(bits >> 24) & 1,
                                     length=(bits >> 25) & 3, type=bits >> 28))
    if nsyms:
        for i in (0, nsyms - 1):
            name, kind, sect, desc, low, high = struct.unpack_from('<IBBHii', data, symoff + i * 16)
            expected.append(dict(nameOffset=name, type=kind, section=sect, description=desc,
                                 valueLow=low, valueHigh=high))
            end = data.index(b'\0', stroff + name, stroff + strsize)
            expected.append(dict(name=data[stroff + name:end].decode()))
    return expected


def verify_macho_reader(probe, path, env):
    result, _ = run([probe, path], env)
    lines = result.stdout.decode().splitlines()
    assert lines[-1] == 'Mach-O reader: PASS' and not result.stderr
    assert [json.loads(line) for line in lines[:-1]] == macho_reader_expected(path), path
    print(f'MACH-O READER ORACLE PASS {path.name} bytes={path.stat().st_size}', flush=True)


def macho_reader_smoke(compiler, root, out, env):
    probe = out / 'native_macho_reader'
    run([compiler, 'build', root / 'src/tests/fixtures/native_macho_reader.vi', '-o', probe], env)
    # Build a structurally valid ARM64 object without an assembler. A symbol
    # name crosses a 4 KiB page and the file exceeds the restricted i32 arena.
    textoff, textsize = 208, 2 * 1024 * 1024 + 4096
    reloff = textoff + textsize
    symoff = reloff + 16
    stroff = (symoff + 32 + 4095) // 4096 * 4096 + 4092
    strings = b'\0_cross_page_symbol\0_external\0'
    data = bytearray(stroff + len(strings))
    struct.pack_into('<8I', data, 0, 0xfeedfacf, 0x100000c, 0, 1, 2, 176, 0, 0)
    struct.pack_into('<II16sQQQQiiII', data, 32, 0x19, 152, b'', 0, textsize,
                     textoff, textsize, 7, 7, 1, 0)
    struct.pack_into('<16s16sQQ8I', data, 104, b'__text', b'__TEXT', 0, textsize,
                     textoff, 2, reloff, 2, 0x80000400, 0, 0, 0)
    struct.pack_into('<6I', data, 184, 2, 24, symoff, 2, stroff, len(strings))
    data[4090:4106] = bytes(range(16))
    struct.pack_into('<4I', data, reloff, 0, 0x2d000000, textsize - 4, 0x2d000001)
    struct.pack_into('<IBBHQ', data, symoff, 1, 15, 1, 0, 0)
    struct.pack_into('<IBBHQ', data, symoff + 16, strings.index(b'_external'), 1, 0, 0, 0)
    data[stroff:] = strings
    obj = out / 'reader-large.o'
    obj.write_bytes(data)
    verify_macho_reader(probe, obj, env)
    # Exercise actual runtime output too (the direct i32 linker is the
    # supported producer for this reader's external relocation subset).
    if env.get('VPP_TEST_NATIVE_RUNTIME_LINK') == '1':
        verify_macho_reader(probe, Path(env['VPP_AOT_VPP_OBJECT']), env)
    mutations = (
        ('magic', 0, 0), ('command-size', 36, 8),
        ('section-in-header', 104 + 48, 32), ('overlap', 104 + 56, textoff),
        ('symbol-table-range', 192, len(data)), ('symbol-section', symoff + 4, 0x0000020f),
        ('symbol-value', symoff + 8, textsize + 1), ('symbol-name-offset', symoff, len(strings)),
        ('reloc-index', reloff + 4, 0x2d000002), ('reloc-width', reloff + 4, 0x2b000000),
        ('reloc-alignment', reloff, 1), ('reloc-range', reloff, textsize),
        ('unterminated-name', stroff + len(strings) - 4, 0x41414141),
    )
    for name, offset, value in mutations:
        malformed = bytearray(data)
        struct.pack_into('<I', malformed, offset, value)
        path = out / f'reader-bad-{name}.o'
        path.write_bytes(malformed)
        result, _ = run([probe, path], env, code=1)
        assert result.stdout.endswith(b'Mach-O reader: REJECT\n') and not result.stderr, name
        path.unlink()
    print(f'MACH-O READER MALFORMED PASS cases={len(mutations)}', flush=True)


def file_seek_smoke(compiler, root, out, env):
    probe = out / 'native_file_seek'
    run([compiler, 'build', root / 'src/tests/fixtures/native_file_seek.vi', '-o', probe], env)
    path = out / 'seek-\u1ec7.bin'
    result, _ = run([probe, path], env)
    assert result.stdout == b'Native file seek: PASS\n' and not result.stderr
    assert not path.exists()
    print('NATIVE FILE SEEK/TELL PASS (signed i32, overflow, buffering, EOF)', flush=True)


def smoke(compiler, root, out, env):
    source = out / 'gói thử nghiệm 𠮷.vi'
    source.write_text('hàm chính() { trả về 42; };\n', encoding='utf-8')
    help_result, _ = run([compiler, '--help'], env)
    assert b'V++ native compiler' in help_result.stdout and not help_result.stderr
    version, _ = run([compiler, '--version'], env)
    assert version.stdout == b'V++ native compiler\n' and not version.stderr
    for args in (['unknown-command'], ['run', source], ['build']):
        result, _ = run([compiler, *args], env, code=1)
        assert not result.stdout and result.stderr
    executable = out / 'probe'
    image = out / 'probe.vmb'
    old = b'previous output must survive\n'
    executable.write_bytes(old)
    invalid_inputs = [('VPP_AOT_VPP_OBJECT', out / 'missing-runtime.o')]
    direct = env.get('VPP_AOT_NATIVE_ARM64_OBJECT') == '1'
    if not direct:
        invalid_inputs.append(('VPP_AOT_ASSEMBLER', out / 'missing-assembler'))
    else:
        invalid_inputs.append(('VPP_AOT_DYNAMIC_BRIDGE_OBJECT', out / 'missing-bridge.o'))
        malformed_bridge = out / 'malformed-bridge.o'
        malformed_bridge.write_bytes(b'not a Mach-O object')
        invalid_inputs.append(('VPP_AOT_DYNAMIC_BRIDGE_OBJECT', malformed_bridge))
    for key, value in invalid_inputs:
        run([compiler, 'build', source, '-o', executable], {**env, key: str(value)}, code=1)
        assert executable.read_bytes() == old
    if env.get('VPP_AOT_NATIVE_MACHO_LINK') == '1':
        malformed = out / 'malformed-platform.o'
        malformed.write_bytes(b'not a Mach-O object')
        for platform in (out / 'missing-platform.o', malformed):
            run([compiler, 'build', source, '-o', executable],
                {**env, 'VPP_AOT_PLATFORM_OBJECT': str(platform)}, code=1)
            assert executable.read_bytes() == old
    before = source.read_bytes()
    alias = out / 'source-alias.vi'
    hardlink = out / 'source-hardlink.vi'
    alias.symlink_to(source)
    os.link(source, hardlink)
    try:
        for path in (source, alias, hardlink):
            run([compiler, 'build', source, '-o', path], env, code=1)
            assert source.read_bytes() == before
    finally:
        alias.unlink()
        hardlink.unlink()
    run([compiler, 'build', source, '-o', executable], env)
    if env.get('VPP_AOT_NATIVE_MACHO_LINK') == '1':
        verify_native_signature(executable.read_bytes())
    run([executable], env, code=42)
    run([compiler, 'vm-build', source, '-o', image], env)
    assert image.stat().st_size > 0
    # Exercise the full language through the autonomous compiler itself,
    # including closures, exception unwinding and dynamic method dispatch.
    for name, code in (('native_return_semantics', 42), ('native_loop_stack_gc', 0),
                       ('native_protected_loop', 0)):
        fixture = root / 'src/tests/fixtures' / (name + '.vi')
        expected = (root / 'src/tests/expected' / (name + '.expected')).read_bytes()
        target = out / name
        run([compiler, 'build', fixture, '-o', target], env)
        result, _ = run([target], env, code=code)
        assert result.stdout == expected and not result.stderr, name
    target = out / 'native_frame_pool_gc'
    run([compiler, 'build', root / 'src/tests/fixtures/native_frame_pool_gc.vi', '-o', target], env)
    result, _ = run([target], env)
    assert result.stdout == '8388608\n8388608\n8388608\n8388608\n42\n43\n[lỗi, 42]\nFrame pool GC: PASS\n'.encode() and not result.stderr
    target = out / 'native_asm_join_gc'
    run([compiler, 'build', root / 'src/tests/fixtures/native_asm_join_gc.vi', '-o', target], env)
    result, _ = run([target], env)
    assert result.stdout == b'ASM join GC: PASS\n' and not result.stderr
    file_seek_smoke(compiler, root, out, env)
    macho_reader_smoke(compiler, root, out, env)
    layout_probe = out / "native_macho_layout"
    run([compiler, "build", root / "src/tests/fixtures/native_macho_layout.vi", "-o", layout_probe], env)
    layout_smoke(layout_probe, out, env)
    patch_probe = out / "native_macho_patch"
    run([compiler, "build", root / "src/tests/fixtures/native_macho_patch.vi", "-o", patch_probe], env)
    patch_smoke(patch_probe, out, env)
    exe_probe = out / "native_macho_executable"
    run([compiler, "build", root / "src/tests/fixtures/native_macho_executable.vi", "-o", exe_probe], env)
    executable_smoke(exe_probe, out, env)
    if not direct:
        asm_stream_parity(compiler, root, out, env)
    else:
        object_probe = out / 'native_macho_dynamic_object'
        run([compiler, 'build', root / 'src/tests/fixtures/native_macho_dynamic_object.vi', '-o', object_probe], env)
        first, second = out / 'dynamic-object-first.o', out / 'dynamic-object-second.o'
        for path in (first, second):
            run([object_probe, path], env)
        assert first.read_bytes() == second.read_bytes()
        parts, symbols = parse_macho_object(first)
        assert parts == [(0, 0, 16, 4, [(0, 0x2d000001)]),
                         (2, 16, 24, 8, [(8, 0x0e000002), (16, 0x0e000001)])]
        assert symbols == [('_caller', 15, 0, 0), ('_callee', 15, 0, 8),
                           ('.local', 14, 1, 16), ('_table', 15, 1, 24)]
        print(f'DIRECT OBJECT PASS sha256={digest(first)}', flush=True)
        paged_probe = out / 'native_macho_paged_relocations'
        run([compiler, 'build', root / 'src/tests/fixtures/native_macho_paged_relocations.vi',
             '-o', paged_probe], env)
        paged = out / 'paged-relocations.o'
        run([paged_probe, paged], env)
        verify_paged_object(paged)
        chunk_probe = out / 'native_arm64_chunked_object'
        run([compiler, 'build', root / 'src/tests/fixtures/native_arm64_chunked_object.vi',
             '-o', chunk_probe], env)
        flat, chunked = out / 'arm64-flat.o', out / 'arm64-chunked.o'
        result, _ = run([chunk_probe, flat, chunked], env)
        assert result.stdout == b'ARM64 chunk parity: PASS\n' and not result.stderr
        assert flat.read_bytes() == chunked.read_bytes()
        parts, symbols = parse_macho_object(chunked)
        assert parts[0][2] > 8192 and len(parts[0][4]) == 1, parts
        call_offset = parts[0][4][0][0]
        assert call_offset >= 8192, parts
        # Decode the two branch immediates independently of the V++ encoder.
        data = chunked.read_bytes()
        text_offset = struct.unpack_from('<I', data, 32 + 72 + 48)[0]
        words = list(struct.iter_unpack('<I', data[text_offset:text_offset + parts[0][2]]))
        cbz = [(i * 4, (w >> 5) & 0x7ffff)
               for i, (w,) in enumerate(words) if w & 0xff000000 == 0x34000000]
        jumps = [(i * 4, w & 0x3ffffff)
                 for i, (w,) in enumerate(words) if w & 0xfc000000 == 0x14000000]
        assert len(cbz) == len(jumps) == 1
        forward, delta = cbz[0]
        backward, back_delta = jumps[0]
        assert delta < 0x40000 and forward + 4 * delta == call_offset
        assert back_delta >= 0x2000000 and backward + 4 * (back_delta - 0x4000000) == forward - 4
        assert forward < 8192 <= backward
        assert words[call_offset // 4][0] == 0x94000000
        print(f'ARM64 CHUNK OBJECT PASS sha256={digest(chunked)}', flush=True)
    assert not list(out.glob('.vpp-aot-*')), out
    print(f'SMOKE PASS {compiler.name} vmb={digest(image)}', flush=True)
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    bootstrap = parser.add_mutually_exclusive_group(required=True)
    bootstrap.add_argument('--stage1', type=Path)
    bootstrap.add_argument('--bootstrap-cli', type=Path, help='Full-language bootstrap compiler used once to produce stage1')
    runtime_input = parser.add_mutually_exclusive_group(required=True)
    runtime_input.add_argument('--runtime', type=Path)
    runtime_input.add_argument('--runtime-seed', type=Path,
                               help='Native i32 seed used to build a fresh V++ runtime object')
    output = parser.add_mutually_exclusive_group(required=True)
    output.add_argument('--output', type=Path)
    output.add_argument('--output-root', type=Path,
                        help='Create a unique run directory under this root (CTest)')
    parser.add_argument('--stage2', type=Path, help='Reuse a stage2 produced from the identical current source by stage1')
    parser.add_argument('--native-runtime-link', action='store_true',
                        help='Verify the direct Mach-O i32 selfhost and consume its converged runtime object (requires --runtime-seed)')
    parser.add_argument('--native-executable-link', action='store_true',
                        help='Require the full-language V++ Mach-O linker; reject any non-assembly clang invocation')
    parser.add_argument('--native-arm64-object', action='store_true',
                        help='Emit ARM64 objects directly; prohibit assembler use during selfhost')
    parser.add_argument('--bridge-object', type=Path, help='Prepared dynamic ABI bridge object')
    parser.add_argument('--platform-object', type=Path,
                        help='Prepared Darwin ARM64 primitives object for the native executable link route')
    parser.add_argument('--smoke-only', action='store_true')
    args = parser.parse_args()
    if args.native_runtime_link and not args.runtime_seed:
        parser.error('--native-runtime-link requires --runtime-seed')
    if args.native_arm64_object and not args.native_executable_link:
        parser.error('--native-arm64-object requires --native-executable-link')
    if args.bridge_object and not args.native_arm64_object:
        parser.error('--bridge-object requires --native-arm64-object')
    if args.platform_object and not args.native_executable_link:
        parser.error('--platform-object requires --native-executable-link')
    root = Path(__file__).resolve().parents[2]
    source = root / 'gói/biên dịch/dòng lệnh AOT tự chủ.vi'
    env = {k: v for k, v in os.environ.items() if not k.startswith('VPP_')}
    if args.output_root:
        output_root = args.output_root.resolve()
        output_root.mkdir(parents=True, exist_ok=True)
        out = Path(tempfile.mkdtemp(prefix='run-', dir=output_root))
    else:
        out = args.output.resolve()
        out.mkdir(parents=True, exist_ok=False)
    print(f'SELFHOST OUTPUT {out}', flush=True)
    if args.native_runtime_link:
        runtime_dir = out / 'native-i32'
        # Rebuild the complete native compiler from the bootstrap seed first.
        # Stage 0 may omit non-host backends; the following stages use the
        # full module tree and the native Mach-O writer/linker.
        seed = out / 'i32-seed'
        _, elapsed = run([args.runtime_seed.resolve(strict=True), 'build',
                          root / 'gói/biên dịch/bản địa/bộ biên dịch.vi', '-o', seed],
                         {**env, 'VPP_HOME': str(root)})
        print(f'I32 SEED BUILD PASS {elapsed:.3f}s', flush=True)
        result, elapsed = run(['/bin/bash', root / 'scripts/quality/check_macos_native_executable_selfhosting.sh',
                               seed, runtime_dir], env, timeout=3600)
        print(result.stdout.decode(errors='replace'), end='', flush=True)
        runtime = (runtime_dir / 'runtime-stage2.o').resolve(strict=True)
        print(f'NATIVE RUNTIME BUILD PASS {elapsed:.3f}s sha256={digest(runtime)}', flush=True)
    elif args.runtime_seed:
        runtime_dir = out / 'runtime'
        result, elapsed = run(['/bin/bash', root / 'scripts/build-vpp-aot-runtime.sh',
                               runtime_dir, args.runtime_seed.resolve(strict=True)], env)
        runtime = (runtime_dir / 'runtime.o').resolve(strict=True)
        print(result.stdout.decode(errors='replace'), end='', flush=True)
        print(f'RUNTIME BUILD PASS {elapsed:.3f}s sha256={digest(runtime)}', flush=True)
    else:
        runtime = args.runtime.resolve(strict=True)
    env.update(PATH='', VPP_HOME=str(root), VPP_AOT_VPP_OBJECT=str(runtime),
               VPP_AOT_ASSEMBLER='/usr/bin/clang')
    if args.native_runtime_link:
        env['VPP_TEST_NATIVE_RUNTIME_LINK'] = '1'
    if args.stage1:
        stage1 = args.stage1.resolve(strict=True)
    else:
        stage1 = out / 'stage1'
        bootstrap_env = {**env, 'PATH': os.environ.get('PATH', ''),
                         'VPP_AOT_RUNTIME': 'vpp', 'VPP_COMPILER_WORKERS': '0'}
        _, elapsed = run([args.bootstrap_cli.resolve(strict=True), 'build', source, '-o', stage1], bootstrap_env)
        print(f'BOOTSTRAP PASS stage1: {elapsed:.3f}s', flush=True)
    if args.native_executable_link:
        wrapper = out / 'assembly-only-clang'
        wrapper.write_text('#!/bin/sh\n[ "${1-}" = -c ] || exit 99\nexec /usr/bin/clang "$@"\n')
        wrapper.chmod(0o700)
        env.update(VPP_AOT_NATIVE_MACHO_LINK='1', VPP_AOT_ASSEMBLER=str(wrapper))
        if args.platform_object:
            env['VPP_AOT_PLATFORM_OBJECT'] = str(args.platform_object.resolve(strict=True))
    if args.native_arm64_object:
        # Setup may assemble only the fixed platform ABI; the compiler
        # under test receives a nonexistent assembler and an empty PATH.
        platform = args.platform_object
        if not platform:
            platform = out / 'platform.o'
            run(['/usr/bin/clang', '-c', root / 'scripts/ASM/macos/vpp_aot_primitives_aarch64.S',
                 '-DVPP_NATIVE_RUNTIME_STACK_ABI=1', '-o', platform])
        bridge = args.bridge_object
        if not bridge:
            bridge = out / 'dynamic-bridge.o'
            run(['/usr/bin/clang', '-c', root / 'scripts/ASM/macos/vpp_aot_dynamic_bridge_aarch64.S',
                 '-o', bridge])
        env.update(VPP_AOT_NATIVE_ARM64_OBJECT='1',
                   VPP_AOT_PLATFORM_OBJECT=str(platform.resolve(strict=True)),
                   VPP_AOT_DYNAMIC_BRIDGE_OBJECT=str(bridge.resolve(strict=True)),
                   VPP_AOT_ASSEMBLER=str(out / 'assembler-must-not-run'))
    audit(stage1)
    smoke1 = out / 'smoke1'
    smoke1.mkdir()
    image1 = smoke(stage1, root, smoke1, env)
    if args.smoke_only:
        return
    stage2 = args.stage2.resolve(strict=True) if args.stage2 else out / 'stage2'
    stage3 = out / 'stage3'
    for parent, child in ((stage1, stage2), (stage2, stage3)):
        if child != stage2 or not args.stage2:
            _, elapsed = run([parent, 'build', source, '-o', child], env)
            print(f'SELF BUILD PASS {parent.name} -> {child.name}: {elapsed:.3f}s', flush=True)
        audit(child)
    for compiler, name in ((stage2, 'smoke2'), (stage3, 'smoke3')):
        directory = out / name
        directory.mkdir()
        assert smoke(compiler, root, directory, env).read_bytes() == image1.read_bytes()
    verify_convergence(stage2, stage3)
    link = 'V++ ARM64 objects, Mach-O linker and ad-hoc signature' if args.native_arm64_object else (
        'V++ Mach-O and ad-hoc signature; system assembly' if args.native_executable_link else 'system clang/ld')
    print(f'FULL_NATIVE_EXECUTABLE_SELFHOST=PASS (prepared runtime; {link})', flush=True)


if __name__ == '__main__':
    main()
