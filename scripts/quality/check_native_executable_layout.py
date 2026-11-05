#!/usr/bin/env python3
"""Inspect loader mappings and imports independently of the V++ serializers.

Usage: check_native_executable_layout.py <native-runtime-executable> [...]
"""
from pathlib import Path
import struct
import sys


def macho(data):
    assert struct.unpack_from("<I", data, 4)[0] == 0x100000c
    assert struct.unpack_from("<I", data, 12)[0] == 2
    position = 32
    segments = []
    dylibs = []
    entry = binding = signature = None
    for _ in range(struct.unpack_from("<I", data, 16)[0]):
        kind, size = struct.unpack_from("<II", data, position)
        assert size >= 8 and size % 8 == 0 and position + size <= len(data)
        if kind == 25:
            name = data[position + 8:position + 24].rstrip(b"\0")
            address, length, offset, count, maximum, protection = struct.unpack_from("<QQQQII", data, position + 24)
            assert offset + count <= len(data) and count <= length
            assert protection & ~maximum == 0 and protection & 6 != 6, name
            segments.append((name, address, length, offset, count, protection))
        elif kind == 12:
            start = position + struct.unpack_from("<I", data, position + 8)[0]
            dylibs.append(data[start:data.index(0, start)])
        elif kind == 0x80000028:
            entry = struct.unpack_from("<Q", data, position + 8)[0]
        elif kind == 0x80000022:
            binding = struct.unpack_from("<II", data, position + 16)
        elif kind == 0x1d:
            signature = struct.unpack_from("<II", data, position + 8)
        position += size
    assert position == 32 + struct.unpack_from("<I", data, 20)[0]
    assert dylibs == [b"/usr/lib/libSystem.B.dylib"]
    assert signature and signature[0] + signature[1] == len(data)
    mapped = [s for s in segments if s[0] != b"__PAGEZERO"]
    for left, right in zip(mapped, mapped[1:]):
        assert left[1] + left[2] <= right[1]
        assert left[3] + left[4] <= right[3]
    text = next(s for s in segments if s[0] == b"__TEXT")
    assert text[3] <= entry < text[3] + text[4] and text[5] & 4
    assert binding and binding[1] > 0
    offset, length = binding
    blob = data[offset:offset + length]
    assert len(blob) == length
    cursor = ordinal = typ = count = 0
    name = None
    while cursor < length:
        operation = blob[cursor]
        cursor += 1
        if operation == 0:
            assert not any(blob[cursor:])
            break
        opcode, immediate = operation & 0xf0, operation & 15
        if opcode == 0x10:
            ordinal = immediate
        elif opcode == 0x50:
            typ = immediate
        elif opcode == 0x40:
            end = blob.index(0, cursor)
            name = blob[cursor:end]
            assert name and not name.startswith(b"_vpp_")
            cursor = end + 1
        elif opcode == 0x70:
            assert immediate < len(segments)
            segment = segments[immediate]
            value = shift = 0
            while True:
                part = blob[cursor]
                cursor += 1
                value |= (part & 127) << shift
                if part < 128:
                    break
                shift += 7
                assert shift < 32
            assert segment[5] & 2 and 0 <= value <= segment[2] - 8
        elif opcode == 0x90:
            assert ordinal == 1 and typ == 1 and name
            count += 1
        else:
            raise AssertionError(f"unrecognized dyld binding opcode {operation:#x}")
    assert count > 30
    return f"MACHO_RUNTIME_LAYOUT=PASS imports={count}"


def pe(data):
    signature = struct.unpack_from("<I", data, 0x3c)[0]
    assert data[signature:signature + 4] == b"PE\0\0"
    machine, sections, timestamp, sym, count, optional, flags = struct.unpack_from("<HHIIIHH", data, signature + 4)
    assert machine == 0x8664 and timestamp == sym == count == 0
    assert optional == 240 and flags & 2
    opt = signature + 24
    assert struct.unpack_from("<H", data, opt)[0] == 0x20b
    entry = struct.unpack_from("<I", data, opt + 16)[0]
    ranges = []
    for i in range(sections):
        start = opt + optional + i * 40
        name, vsize, rva, size, offset, _, _, _, _, characteristics = struct.unpack_from("<8sIIIIIIHHI", data, start)
        assert offset + size <= len(data) and offset % 512 == 0 and rva % 4096 == 0
        assert characteristics & 0xa0000000 != 0xa0000000, name
        ranges.append((rva, vsize, offset, size, characteristics))
    def file_offset(rva, length=1):
        matches = [s for s in ranges if s[0] <= rva and rva + length <= s[0] + min(s[1], s[3])]
        assert len(matches) == 1, rva
        return matches[0][2] + rva - matches[0][0]
    assert any(s[0] <= entry < s[0] + s[1] and s[4] & 0x20000000 for s in ranges)
    def string(rva):
        start = file_offset(rva)
        return data[start:data.index(0, start)].decode("ascii")
    imports, size = struct.unpack_from("<II", data, opt + 112 + 8)
    table = file_offset(imports, size)
    dlls = set()
    total = 0
    for i in range(size // 20):
        lookup, timestamp, forwarder, name, iat = struct.unpack_from("<IIIII", data, table + i * 20)
        if (lookup, timestamp, forwarder, name, iat) == (0, 0, 0, 0, 0):
            break
        assert timestamp == forwarder == 0
        dlls.add(string(name).lower())
        for j in range(256):
            hint = struct.unpack_from("<Q", data, file_offset(lookup + j * 8, 8))[0]
            pointer = struct.unpack_from("<Q", data, file_offset(iat + j * 8, 8))[0]
            assert pointer == hint
            if hint == 0:
                break
            symbol = string(hint + 2)
            assert symbol and not symbol.startswith("vpp_")
            total += 1
        else:
            raise AssertionError("unterminated import lookup table")
    else:
        raise AssertionError("unterminated import descriptor table")
    assert dlls == {"kernel32.dll", "msvcrt.dll", "shell32.dll", "bcrypt.dll"}
    assert total > 30
    return f"PE_RUNTIME_LAYOUT=PASS imports={total}"


for name in sys.argv[1:]:
    data = Path(name).read_bytes()
    print(macho(data) if data[:4] == b"\xcf\xfa\xed\xfe" else pe(data), name)
