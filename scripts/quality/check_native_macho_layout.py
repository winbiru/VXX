#!/usr/bin/env python3
"""Independent Mach-O layout oracle and synthetic cross-object regressions."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
GROUPS = [(b'__TEXT', b'__text'), (b'__TEXT', b'__cstring'),
          (b'__DATA', b'__const'), (b'__DATA', b'__bss')]


def catalog():
    # Use the same explicit platform policy as the i32 linker. Never derive
    # permitted imports from the objects being tested.
    source = (ROOT / 'gói/biên dịch/bản địa/liên kết macho runtime.vi').read_text()
    names = re.search(r'biến danh sách = "\|([^"\n]+)\|";', source).group(1).split('|')
    writer = (ROOT / 'gói/biên dịch/xuất macho.vi').read_text()
    full_names = re.search(r'tách hằng văn bản\("([^"\n]+)", "\|"\)', writer).group(1).split('|')
    assert full_names == names, 'full and restricted Mach-O import policy differs'
    return names


def parse(path):
    data = path.read_bytes()
    magic, cpu, subtype, kind, count, _, _, _ = struct.unpack_from('<8I', data)
    assert (magic, cpu, subtype, kind) == (0xfeedfacf, 0x100000c, 0, 1)
    pos, sections, symbols = 32, [], []
    for _ in range(count):
        command, length = struct.unpack_from('<II', data, pos)
        if command == 25:
            for i in range(struct.unpack_from('<I', data, pos + 64)[0]):
                name, seg, addr, size, _, align, rel, nrel, *_ = struct.unpack_from(
                    '<16s16sQQ8I', data, pos + 72 + 80 * i)
                relocations = [struct.unpack_from('<II', data, rel + 8 * j) for j in range(nrel)]
                sections.append((GROUPS.index((seg.rstrip(b'\0'), name.rstrip(b'\0'))),
                                 addr, size, 1 << align, relocations))
        elif command == 2:
            symoff, nsyms, stroff, strsize = struct.unpack_from('<4I', data, pos + 8)
            for i in range(nsyms):
                name, kind, sec, desc, value = struct.unpack_from('<IBBHQ', data, symoff + i * 16)
                name = data[stroff + name:data.index(b'\0', stroff + name, stroff + strsize)].decode()
                assert desc == 0 and kind in (1, 14, 15)
                symbols.append((name, kind, sec - 1, value))
        pos += length
    return sections, symbols


def oracle(paths, allowed):
    objects = [parse(path) for path in paths]
    definitions = {}
    for i, (sections, symbols) in enumerate(objects):
        for name, kind, sec, value in symbols:
            if kind == 15:
                assert name not in definitions
                definitions[name] = (i, sec, value - sections[sec][1])
    imports = list(dict.fromkeys(name for _, symbols in objects for name, kind, _, _ in symbols
                                 if kind == 1 and name not in definitions))
    assert set(imports) <= set(allowed)
    pos, places, layout = 16384, {}, []
    align = lambda value, amount: (value + amount - 1) // amount * amount
    for group in range(4):
        if group == 2:
            pointers = data = align(pos, 16384)
            pos = pointers + len(imports) * 8
        if group == 3:
            file_end = pos
        for i, (sections, _) in enumerate(objects):
            for j, (kind, _, size, alignment, _) in enumerate(sections):
                if kind == group:
                    pos = align(pos, alignment)
                    places[i, j] = pos
                    layout.append([i, j, group, pos, size])
                    pos += size
        if group == 0:
            stub = align(pos, 4)
            pos = stub + len(imports) * 12
    i, j, offset = definitions['_main']
    entry = places[i, j] + offset
    summaries = []
    for i, (sections, symbols) in enumerate(objects):
        addresses = []
        for name, kind, sec, value in symbols:
            if kind != 1:
                addr = places[i, sec] + value - sections[sec][1]
            elif name in definitions:
                obj, part, offset = definitions[name]
                addr = places[obj, part] + offset
            else:
                addr = stub + imports.index(name) * 12
            addresses.append(addr)
        for j, (_, _, _, _, relocations) in enumerate(sections):
            counts, checksum = [0] * 5, 0
            for offset, bits in relocations:
                kind, symbol = bits >> 28, bits & 0xffffff
                counts[kind] += 1
                for n in (places[i, j] + offset, addresses[symbol], kind):
                    checksum = (checksum * 31 + n) % 1000003
            summaries.append([i, j, counts, checksum])
    return [layout, imports, [stub, pointers, data, file_end, pos, entry], summaries]


def verify(probe, paths, env=None, allowed=None):
    allowed = catalog() if allowed is None else allowed
    start = time.monotonic()
    result = subprocess.run([str(probe), '|'.join(allowed), *map(str, paths)], env=env,
                            capture_output=True, timeout=180)
    assert result.returncode == 0 and not result.stderr, (result.returncode, result.stdout, result.stderr)
    lines = result.stdout.decode().splitlines()
    assert lines[-1] == 'Mach-O layout: PASS', lines
    actual = [json.loads(line) for line in lines[:-1]]
    expected = oracle(paths, allowed)
    assert actual == expected, (actual, expected)
    total = sum(sum(s[2]) for s in actual[3])
    print(f'MACH-O LAYOUT ORACLE PASS objects={len(paths)} relocations={total} '
          f'imports={len(actual[1])} image={actual[2][4]} bytes {time.monotonic()-start:.3f}s', flush=True)


def object_file(path, parts, symbols):
    # Minimal MH_OBJECT producer independent of the compiler and assembler.
    # parts: (group, byte size, alignment exponent, [(offset, type, symbol)])
    # symbols: (name, kind, section ordinal, section-relative value)
    commands_size = 72 + len(parts) * 80 + 24
    data = bytearray(32 + commands_size)
    struct.pack_into('<8I', data, 0, 0xfeedfacf, 0x100000c, 0, 1, 2, commands_size, 0, 0)
    addresses = []
    address = 0
    for i, (group, size, exponent, relocs) in enumerate(parts):
        address = (address + (1 << exponent) - 1) & -(1 << exponent)
        addresses.append(address)
        segment, name = GROUPS[group]
        offset = len(data) if group != 3 else 0
        if group != 3:
            data.extend(bytes(size))
            for where, kind, _ in relocs:
                if kind:
                    struct.pack_into('<I', data, offset + where,
                                     {2: 0x94000000, 3: 0x90000009, 4: 0x91000129}[kind])
        reloff = len(data) if relocs else 0
        for where, kind, symbol in relocs:
            bits = symbol | (int(kind in (2, 3)) << 24) | ((3 if kind == 0 else 2) << 25) | (1 << 27) | (kind << 28)
            data.extend(struct.pack('<II', where, bits))
        flags = [0x80000400, 2, 0, 1][group]
        struct.pack_into('<16s16sQQ8I', data, 104 + i * 80, name, segment, address, size,
                         offset, exponent, reloff, len(relocs), flags, 0, 0, 0)
        address += size
    symoff = len(data)
    strings = bytearray(b'\0')
    for name, kind, section, value in symbols:
        index = len(strings)
        strings.extend(name.encode() + b'\0')
        data.extend(struct.pack('<IBBHQ', index, kind, section, 0,
                                value + addresses[section-1] if section else value))
    stroff = len(data)
    data.extend(strings)
    struct.pack_into('<II16sQQQQiiII', data, 32, 25, 72 + 80 * len(parts), b'',
                     0, address, 32 + commands_size, symoff - 32 - commands_size, 7, 7, len(parts), 0)
    struct.pack_into('<6I', data, 104 + 80 * len(parts), 2, 24, symoff, len(symbols), stroff, len(strings))
    path.write_bytes(data)
    return path


def smoke(probe, out, env=None):
    one = object_file(out / 'layout-program.o', [(0, 16, 2, [(0, 2, 2), (4, 2, 3), (8, 3, 1), (12, 4, 1)]),
                                                (2, 8, 3, [(0, 0, 0)])],
                      [('_main', 15, 1, 0), ('local', 14, 2, 0), ('_helper', 1, 0, 0), ('_write', 1, 0, 0)])
    two = object_file(out / 'layout-runtime.o', [(0, 8, 3, [(0, 2, 2)]), (1, 16, 4, []), (3, 8, 3, [])],
                      [('_helper', 15, 1, 0), ('local', 14, 2, 0), ('_main', 1, 0, 0)])
    verify(probe, [one, two], env)
    missing = object_file(out / 'layout-missing-entry.o', [(0, 4, 2, [])], [('_helper', 15, 1, 0)])
    bad = object_file(out / 'layout-bad-reloc.o', [(0, 8, 3, [(0, 0, 0)])], [('_main', 15, 1, 0)])
    import_page = object_file(out / 'layout-import-page.o', [(0, 4, 2, [(0, 3, 1)])],
                              [('_main', 15, 1, 0), ('_write', 1, 0, 0)])
    for paths, allowed, message in (([one], catalog(), 'không tìm thấy ký hiệu'),
                                    ([one, two], [], 'không tìm thấy ký hiệu'),
                                    ([one, two, two], catalog(), 'ký hiệu trùng'),
                                    ([missing], catalog(), 'thiếu _main'),
                                    ([bad], catalog(), 'relocation chưa hỗ trợ'),
                                    ([import_page], catalog(), 'relocation chưa hỗ trợ')):
        result = subprocess.run([str(probe), '|'.join(allowed), *map(str, paths)], env=env,
                                capture_output=True, timeout=30)
        assert result.returncode == 1 and message in result.stdout.decode() and not result.stderr, result
    print('MACH-O LAYOUT REJECTION PASS cases=6', flush=True)


def section_file_offsets(path):
    data = path.read_bytes()
    pos, offsets = 32, []
    for _ in range(struct.unpack_from('<I', data, 16)[0]):
        command, size = struct.unpack_from('<II', data, pos)
        if command == 25:
            offsets.extend(struct.unpack_from('<I', data, pos + 72 + 80*i + 48)[0]
                           for i in range(struct.unpack_from('<I', data, pos + 64)[0]))
        pos += size
    return data, offsets


def patch_oracle(paths, allowed):
    layout, imports, (stub, *_), _ = oracle(paths, allowed)
    placed = {(i, j): start for i, j, _, start, _ in layout}
    objects = [parse(path) for path in paths]
    globals_ = {name: placed[i, sec] + value - sections[sec][1]
                for i, (sections, symbols) in enumerate(objects)
                for name, kind, sec, value in symbols if kind == 15}
    result = []
    for i, (sections, symbols) in enumerate(objects):
        data, offsets = section_file_offsets(paths[i])
        targets = [placed[i, sec] + value - sections[sec][1] if kind != 1 else
                   globals_[name] if name in globals_ else stub + imports.index(name) * 12
                   for name, kind, sec, value in symbols]
        for j, (_, _, _, _, relocations) in enumerate(sections):
            result.append([i, j, len(relocations)])
            for offset, bits in relocations:
                target = targets[bits & 0xffffff]
                kind = bits >> 28
                loc = placed[i, j] + offset
                word = struct.unpack_from('<I', data, offsets[j] + offset)[0]
                if kind == 0:
                    assert struct.unpack_from('<Q', data, offsets[j] + offset)[0] == 0
                    result.append([loc, target, 1])
                    continue
                if kind == 2:
                    assert word in (0x14000000, 0x94000000)
                    word |= ((target - loc) >> 2) & 0x3ffffff
                elif kind == 3:
                    assert word & ~31 == 0x90000000
                    page_delta = ((target >> 12) - (loc >> 12)) & 0x1fffff
                    word |= (page_delta & 3) << 29 | (page_delta >> 2) << 5
                elif kind == 4:
                    assert word & 0x003ffc00 == 0
                    scale = 1 if word >> 24 == 0x91 else 1 << (word >> 30)
                    assert (target & 4095) % scale == 0
                    word |= ((target & 4095) // scale) << 10
                else:
                    raise AssertionError(kind)
                result.append([loc, struct.unpack('<i', struct.pack('<I', word))[0], 0])
    return result


def verify_patches(probe, paths, env=None, allowed=None):
    allowed = catalog() if allowed is None else allowed
    start = time.monotonic()
    result = subprocess.run([str(probe), '|'.join(allowed), *map(str, paths)], env=env,
                            capture_output=True, timeout=180)
    assert result.returncode == 0 and not result.stderr, (result.returncode, result.stdout[-1000:], result.stderr)
    lines = result.stdout.decode().splitlines()
    assert lines[-1] == 'Mach-O patch: PASS', lines[-5:]
    actual = [json.loads(line) for line in lines[:-1]]
    expected = patch_oracle(paths, allowed)
    assert len(actual) == len(expected), (len(actual), len(expected))
    for i, (got, want) in enumerate(zip(actual, expected)):
        assert got == want, (i, got, want)
    count = sum(len(s[4]) for p in paths for s in parse(p)[0])
    rebases = sum(1 for p in paths for s in parse(p)[0] for _, b in s[4] if b >> 28 == 0)
    print(f'MACH-O PATCH EXACT ORACLE PASS relocations={count} rebases={rebases} '
          f'{time.monotonic()-start:.3f}s', flush=True)


def patch_smoke(probe, out, env=None):
    one, two = out / 'layout-program.o', out / 'layout-runtime.o'
    verify_patches(probe, [one, two], env)
    data, offsets = section_file_offsets(one)
    cases = []
    for tag, location, word, error in (
        ('branch-addend', offsets[0], 0x94000001, 'BRANCH26 opcode'),
        ('page21-addend', offsets[0]+8, 0x90000029, 'PAGE21 opcode'),
        ('pageoff-addend', offsets[0]+12, 0x91000529, 'PAGEOFF12 addend'),
        ('pointer-addend', offsets[1], 1, 'UNSIGNED addend'),
    ):
        mutated = bytearray(data)
        struct.pack_into('<I', mutated, location, word)
        path = out / f'patch-{tag}.o'
        path.write_bytes(mutated)
        cases.append(([path, two], error))
    duplicate = object_file(out / 'patch-duplicate.o', [(0, 4, 2, [(0, 2, 0), (0, 2, 0)])],
                            [('_main', 15, 1, 0)])
    cases.append(([duplicate], 'relocation trùng vị trí'))
    for paths, error in cases:
        result = subprocess.run([str(probe), '|'.join(catalog()), *map(str, paths)], env=env,
                                capture_output=True, timeout=30)
        assert result.returncode == 1 and error in result.stdout.decode() and not result.stderr, result
    print(f'MACH-O PATCH REJECTION PASS cases={len(cases)}', flush=True)


def verify_native_signature(raw):
    """Independently check every code slot, including the finalized header."""
    pos = 32
    signatures = []
    for _ in range(struct.unpack_from('<I', raw, 16)[0]):
        cmd, length = struct.unpack_from('<II', raw, pos)
        if cmd == 29:
            signatures.append(struct.unpack_from('<II', raw, pos + 8))
        pos += length
    assert len(signatures) == 1
    off, size = signatures[0]
    assert off % 16 == 0 and off + size == len(raw)
    magic, length, count, slot, relative = struct.unpack_from('>5I', raw, off)
    assert (magic, count, slot, relative) == (0xfade0cc0, 1, 0, 20)
    cd = off + relative
    fields = struct.unpack_from('>9I4B4I4Q', raw, cd)
    magic, cdlen, version, flags, hash_off, ident, special, pages, limit = fields[:9]
    assert (magic, version, flags, special, limit) == (0xfade0c02, 0x20400, 2, 0, off)
    assert fields[9:13] == (32, 2, 0, 14)
    assert raw[cd+ident:cd+hash_off] == b'org.vpp.native\0'
    assert pages == (off + 16383) // 16384 and cdlen == hash_off + pages * 32
    assert length == 20 + cdlen and not any(raw[off+length:])
    for i in range(pages):
        expected = hashlib.sha256(raw[i*16384:min((i+1)*16384, off)]).digest()
        assert raw[cd+hash_off+i*32:cd+hash_off+(i+1)*32] == expected, i
    return off


def executable_smoke(probe, out, env=None, large=False):
    """Run independently constructed ARM64 objects through the V++ writer."""
    def instructions(path, words):
        data, offsets = section_file_offsets(path)
        data = bytearray(data)
        struct.pack_into('<' + 'I' * len(words), data, offsets[0], *words)
        path.write_bytes(data)

    def link(paths, dest, expected, timeout=180):
        result = subprocess.run([str(probe), '|'.join(catalog()), str(dest), *map(str, paths)],
                                env=env, capture_output=True, timeout=timeout)
        assert result.returncode == 0 and result.stdout == b'Mach-O executable: PASS\n' and not result.stderr, result
        raw = dest.read_bytes()
        assert struct.unpack_from('<4I', raw) == (0xfeedfacf, 0x100000c, 0, 2)
        # Validate all patched words directly in the actual emitted file.
        patches = patch_oracle(paths, catalog())
        pos = 0
        while pos < len(patches):
            _, _, count = patches[pos]
            pos += 1
            for address, low, high in patches[pos:pos+count]:
                assert struct.unpack_from('<I', raw, address)[0] == low % (1 << 32)
                if high:
                    assert struct.unpack_from('<I', raw, address + 4)[0] == high
            pos += count
        # The executable must retain all strong external definitions for
        # native symbol auditing, including functions outside _main.
        layout, _, _, _ = oracle(paths, catalog())
        placed = {(i, j): address for i, j, _, address, _ in layout}
        symbols = {}
        for i, path in enumerate(paths):
            parts, entries = parse(path)
            for name, kind, sec, value in entries:
                if kind == 15:
                    symbols[name] = 0x100000000 + placed[i, sec] + value - parts[sec][1]
        actual = {}
        pos = 32
        for _ in range(struct.unpack_from('<I', raw, 16)[0]):
            command, length = struct.unpack_from('<II', raw, pos)
            if command == 2:
                symoff, count, stroff, strsize = struct.unpack_from('<4I', raw, pos + 8)
                for i in range(count):
                    name, kind, sec, desc, value = struct.unpack_from('<IBBHQ', raw, symoff + i * 16)
                    assert kind == 15 and sec > 0 and desc == 0
                    end = raw.index(b'\0', stroff + name, stroff + strsize)
                    actual[raw[stroff + name:end].decode()] = value
            pos += length
        assert actual == symbols
        verify_native_signature(raw)
        dest.chmod(0o700)
        subprocess.run(['/usr/bin/codesign', '--verify', str(dest)], check=True, capture_output=True, env=env)
        result = subprocess.run([str(dest)], env=env, capture_output=True, timeout=30)
        assert result.returncode == expected and not result.stdout and not result.stderr, result
        return dest.read_bytes()

    minimal = object_file(out / 'exe-return42.o', [(0, 8, 2, [])], [('_main', 15, 1, 0)])
    instructions(minimal, [0x52800540, 0xd65f03c0])  # mov w0,#42; ret
    raw = link([minimal], out / 'exe-return42', 42)
    tampered = bytearray(raw)
    tampered[16384] ^= 1
    bad = out / 'exe-tampered'
    bad.write_bytes(tampered)
    result = subprocess.run(['/usr/bin/codesign', '--verify', str(bad)], capture_output=True, env=env)
    assert result.returncode != 0, 'tampered code accepted'
    try:
        verify_native_signature(tampered)
    except AssertionError:
        pass
    else:
        raise AssertionError('independent verifier accepted tampered code')
    # Call a harmless libSystem import, a cross-object function, and the
    # same function through a dyld-rebased pointer in writable data.
    words = [0xa9bf7bfd, 0x12800000, 0x94000000, 0x94000000,
             0x90000009, 0x91000129, 0xf9400129, 0xd63f0120,
             0xa8c17bfd, 0xd65f03c0]
    program = object_file(out / 'exe-program.o',
                          [(0, len(words)*4, 2, [(8, 2, 1), (12, 2, 2), (16, 3, 3), (20, 4, 3)]),
                           (2, 8, 3, [(0, 0, 2)])],
                          [('_main', 15, 1, 0), ('_close', 1, 0, 0),
                           ('_helper', 1, 0, 0), ('local', 14, 2, 0)])
    instructions(program, words)
    helper = object_file(out / 'exe-helper.o', [(0, 8, 2, [])], [('_helper', 15, 1, 0)])
    instructions(helper, [0x52800540, 0xd65f03c0])
    one = link([program, helper], out / 'exe-cross-one', 42)
    two = link([program, helper], out / 'exe-cross-two', 42)
    assert one == two, 'signed executable differs between builds'
    # The preceding 8-byte entry section shifts this text section off a
    # page boundary. Exercise adjacent relocations on both sides of the
    # absolute page boundary, deliberately in reverse and mixed order.
    pages = object_file(out / 'exe-pages.o',
                        [(0, 8200, 2, [(8192, 2, 0), (4088, 2, 0), (4084, 2, 0), (0, 2, 0)]),
                         (2, 8200, 3, [(8192, 0, 0), (0, 0, 0), (4096, 0, 0), (4088, 0, 0)])],
                        [('_main', 1, 0, 0)])
    link([minimal, pages], out / 'exe-pages', 42)
    print('MACH-O EXECUTABLE PASS return=42 imports=1 rebases=1 deterministic=1 page_boundaries=1', flush=True)
    if large:
        # Exercise the writer's long-lived frame beyond the former stack
        # failure at ~88k text relocations. The entry exits before this
        # section, so the oracle can validate every patched word safely.
        count = 90000
        order = list(range(0, count, 2)) + list(range(count - 1, 0, -2))
        stress = object_file(out / 'exe-large-relocations.o',
                             [(0, count * 4, 2, [(i * 4, 2, 0) for i in order])],
                             [('_main', 1, 0, 0)])
        link([minimal, stress], out / 'exe-large-relocations', 42, timeout=1800)
        print(f'MACH-O LARGE EXECUTABLE PASS relocations={count}', flush=True)



if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', type=Path)
    parser.add_argument('objects', nargs='+', type=Path)
    args = parser.parse_args()
    verify(args.probe.resolve(), args.objects)
