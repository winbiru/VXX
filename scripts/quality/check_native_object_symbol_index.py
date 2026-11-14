#!/usr/bin/env python3
"""Check relocation order in objects emitted by native_object_symbol_index.vi."""
import struct
import sys
from pathlib import Path

elf = Path(sys.argv[1]).read_bytes()
assert elf[:4] == b"\x7fELF"
section_table = struct.unpack_from("<Q", elf, 40)[0]
rela_offset, rela_size = struct.unpack_from("<QQ", elf, section_table + 2 * 64 + 24)
assert rela_size == 6 * 24
assert [struct.unpack_from("<QQq", elf, rela_offset + i * 24) for i in range(6)] == [
    (offset, (index << 32) | 4, -4)
    for offset, index in zip(range(0, 24, 4), [4, 3, 4, 6, 5, 6])
]
coff = Path(sys.argv[2]).read_bytes()
assert struct.unpack_from("<HH", coff) == (0x8664, 1)
assert struct.unpack_from("<I", coff, 12)[0] == 6
rel_offset = struct.unpack_from("<I", coff, 20 + 24)[0]
assert struct.unpack_from("<H", coff, 20 + 32)[0] == 6
assert [struct.unpack_from("<IIH", coff, rel_offset + i * 10) for i in range(6)] == [
    (offset, index, 4)
    for offset, index in zip(range(0, 24, 4), [3, 2, 3, 5, 4, 5])
]
print("NATIVE_OBJECT_SYMBOL_INDEX=PASS (ELF/COFF)")
