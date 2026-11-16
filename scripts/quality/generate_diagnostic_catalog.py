#!/usr/bin/env python3
"""Regenerate or verify the compact V++ runtime diagnostic catalogs.

The numbered comments are the editable source of truth; the packed words are
generated so the raw native compiler need not emit an instruction per UTF-8 byte.
"""
import argparse
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[2]
CATALOGS = ('mã lỗi.vi', 'mã thao tác.vi', 'thông báo.vi')


def rows(source):
    entries = {}
    for key, value in re.findall(r'^// (\d+): (.+)$', source, re.M):
        key = int(key)
        if key in entries or '\0' in value:
            raise ValueError(f'Duplicate or invalid catalog entry: {key}')
        entries[key] = value
    if not entries:
        raise ValueError('Empty catalog')
    return entries


def compress(raw):
    # LZSS, flags low bit first; 11-bit distance, 5-bit length.
    out = bytearray()
    i = 0
    while i < len(raw):
        flag = len(out)
        out.append(0)
        for bit in range(8):
            if i == len(raw):
                break
            best, distance = 0, 0
            for j in range(max(0, i - 2048), i):
                n = 0
                while n < 34 and i + n < len(raw) and raw[j + n] == raw[i + n]:
                    n += 1
                if n > best:
                    best, distance = n, i - j
            if best >= 3:
                out[flag] |= 1 << bit
                out.extend(struct.pack('<H', (distance - 1) * 32 + best - 3))
                i += best
            else:
                out.append(raw[i])
                i += 1
    return out


def regenerate(source):
    entries = rows(source)
    raw = ('\0'.join(entries.get(i, '') for i in range(max(entries) + 1)) + '\0').encode('utf-8')
    packed = compress(raw)
    words = [int.from_bytes(packed[i:i+4].ljust(4, b'\0'), 'little', signed=True)
             for i in range(0, len(packed), 4)]
    words += [0] * (-len(words) % 7)
    def literal(n):
        return str(n) if n != -2147483648 else '(0 - 2147483647 - 1)'
    data = ''.join('    chẩn đoán nạp bảng(bảng, ' +
                   ', '.join(map(literal, words[i:i+7])) + ');\n'
                   for i in range(0, len(words), 7))
    result, count = re.subn(r'(?:    chẩn đoán nạp bảng\(bảng, .+\);\n)+', data, source)
    if count != 1:
        raise ValueError('Missing or repeated generated data block')
    result, count = re.subn(r'chẩn đoán tra bảng\(bảng, \d+, \d+,',
                            f'chẩn đoán tra bảng(bảng, {len(packed)}, {len(raw)},', result)
    if count != 1:
        raise ValueError('Missing or repeated table lookup')
    # These two public lookups guard the numeric ABI before allocating a table.
    result = re.sub(r'(nếu \(mã >= 0 && mã <= )\d+(\) \{)',
                    lambda match: match[1] + str(max(entries)) + match[2], result)
    return result


def verify_abi(root):
    compiler = (root / 'gói/biên dịch/mã thao tác.vi').read_text(encoding='utf-8')
    pairs = re.findall(r'ca "(OP_[A-Za-z0-9_]+)":.*?trả về (\d+);', compiler)
    # Historical Logic_VA/Logic_HOAC spellings use mixed case in the
    # compiler; diagnostics follow the uppercase disassembler spelling.
    expected = {int(k): v.upper() for v, k in pairs}
    if len(expected) != len(pairs) or not expected:
        raise ValueError('Malformed compiler opcode catalog')
    reverse = (root / 'gói/biên dịch/giải mã bytecode.vi').read_text(encoding='utf-8')
    for key, value in re.findall(r'ca (\d+): \{ trả về "(OP_[A-Za-z0-9_]+)";', reverse):
        key = int(key)
        if key in expected and expected[key] != value:
            raise ValueError(f'Conflicting compiler opcode name: {key}')
        expected[key] = value
    actual = rows((root / 'gói/chẩn đoán/mã thao tác.vi').read_text(encoding='utf-8'))
    if actual != expected:
        raise ValueError('Diagnostic opcode catalog differs from compiler ABI')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--root', type=Path, default=ROOT)
    args = parser.parse_args()
    verify_abi(args.root)
    for name in CATALOGS:
        path = args.root / 'gói/chẩn đoán' / name
        source = path.read_text(encoding='utf-8')
        generated = regenerate(source)
        if args.write:
            path.write_text(generated, encoding='utf-8')
        elif source != generated:
            raise SystemExit(f'Outdated diagnostic catalog: {name}; use --write')
    print('V++ diagnostic catalogs: packed data and opcode ABI match')


if __name__ == '__main__':
    main()
