#!/usr/bin/env python3
"""Generate and run bounded ELF/linker failure regressions (no assembler).

Arguments: <native-linker harness> <valid runtime-object.o> <output-dir>.
The .o is a fixture prepared separately from the external-tool-free build gate.
"""
import os
from pathlib import Path
import struct
import subprocess
import sys
import shlex

harness, object_path, output_path = sys.argv[1:]
out = Path(output_path)
out.mkdir(parents=True, exist_ok=True)
original = Path(object_path).read_bytes()
runner = shlex.split(os.environ.get('VPP_ELF_TEST_RUNNER', ''))
remote_root = os.environ.get('VPP_ELF_TEST_REMOTE_ROOT')

def execution_path(path):
    return str(Path(remote_root) / path.name) if remote_root else str(path)

def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]

def u64(data, offset):
    return struct.unpack_from('<Q', data, offset)[0]

def wrap(data):
    return struct.pack('<I', len(data)) + data

sections = u64(original, 40)
count = struct.unpack_from('<H', original, 60)[0]
text = next(sections + i * 64 for i in range(1, count)
            if u64(original, sections + i * 64 + 8) & 4)
rela = next(sections + i * 64 for i in range(1, count)
            if u32(original, sections + i * 64 + 4) == 4)
symtab = next(sections + i * 64 for i in range(1, count)
              if u32(original, sections + i * 64 + 4) == 2)
relocation = u64(original, rela + 24)
variants = {'valid': wrap(original), 'truncated': wrap(original)[:-1],
            'duplicate': wrap(original) + wrap(original)}
for name, offset, fmt, value in [
    ('header-high-word', 44, '<I', 1),
    ('writable-code', text + 8, '<Q', 7),
    ('bad-alignment', text + 48, '<Q', 3),
    ('rela-wrong-symtab', rela + 40, '<I', 0),
    ('rela-wrong-entry-size', rela + 56, '<Q', 16),
    ('unknown-relocation', relocation + 8, '<I', 12345),
    ('relocation-out-of-section', relocation, '<Q', 0x100000),
    ('relocation-addend-high', relocation + 20, '<I', 1),
]:
    modified = bytearray(original)
    struct.pack_into(fmt, modified, offset, value)
    variants[name] = wrap(modified)
# Force a positive R_X86_64_64 result outside the linker's low i32 arena ABI.
modified = bytearray(original)
struct.pack_into('<I', modified, relocation + 8, 1)
struct.pack_into('<q', modified, relocation + 16, 0x7fffffff)
variants['absolute-address-overflow'] = wrap(modified)
# _start in the host requires main; preserve the symbol table's framing while
# removing that definition, to exercise undefined-symbol resolution.
modified = bytearray(original)
strings_index = u32(original, symtab + 40)
strings = u64(original, sections + strings_index * 64 + 24)
for offset in range(u64(original, symtab + 24),
                    u64(original, symtab + 24) + u64(original, symtab + 32), 24):
    name_offset = strings + u32(original, offset)
    if original[name_offset:name_offset + 5] == b'main\0':
        modified[name_offset] = ord('x')
        break
else:
    raise AssertionError('fixture has no main definition')
variants['unresolved-main'] = wrap(modified)

environment = dict(os.environ, VPP_NATIVE_ELF_LINK='1')
for name, archive in variants.items():
    archive_path = out / (name + '.archive')
    destination = out / (name + '.exe')
    archive_path.write_bytes(archive)
    sentinel = b'existing output must survive link failure\n'
    destination.write_bytes(sentinel)
    result = subprocess.run(runner + [harness, execution_path(archive_path), execution_path(destination)],
                            env=environment, capture_output=True, timeout=120)
    (out / (name + '.log')).write_bytes(result.stdout + result.stderr)
    if name == 'valid':
        assert result.returncode == 0, (name, result.returncode, result.stderr)
        assert subprocess.run(runner + [execution_path(destination)], timeout=30).returncode == 42
    else:
        assert result.returncode == 1, (name, result.returncode, result.stderr)
        assert destination.read_bytes() == sentinel, name
    assert not list(out.glob(name + '.exe.vpp-exe-*')), name
    print('NATIVE_ELF_INPUT_' + name.upper().replace('-', '_') + '=PASS', flush=True)
print('NATIVE_ELF_MALFORMED_INPUTS=PASS', flush=True)
