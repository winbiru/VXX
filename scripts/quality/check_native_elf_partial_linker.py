#!/usr/bin/env python3
"""Exercise V++'s ET_REL-to-ET_REL linker without invoking external build tools.

Usage: check_native_elf_partial_linker.py --self-test <output-dir>
       check_native_elf_partial_linker.py <merge-harness> <exec-harness> <output-dir>

The fixtures are constructed as ELF64 ET_REL bytes, so no assembler or C
compiler is needed. The second harness performs final native ET_EXEC linking.
"""
from pathlib import Path
import os
import struct
import subprocess
import sys

SECTION_NAMES = b"\0.text\0.rela.text\0.symtab\0.strtab\0.shstrtab\0.note.GNU-stack\0"


def align(value, step):
    return (value + step - 1) // step * step


def elf_object(code, definitions=(), imports=(), calls=()):
    """Make precisely the seven-section compiler ELF object layout."""
    symbols = [(name, True, offset) for name, offset in definitions]
    symbols += [(name, False, 0) for name in imports]
    names = b"\0"
    symtab = bytearray(24)
    for name, defined, value in symbols:
        assert name and b"\0" not in name and 0 <= value < len(code)
        symtab += struct.pack("<IBBHQQ", len(names), 18, 0, int(defined), value, 0)
        names += name + b"\0"
    relocations = b""
    for offset, name in calls:
        index = next(i for i, sym in enumerate(symbols, start=1) if sym[0] == name)
        assert 0 <= offset <= len(code) - 4
        relocations += struct.pack("<QQq", offset, (index << 32) | 4, -4)

    result = bytearray(64)
    result += code
    result += b"\0" * (align(len(result), 8) - len(result))
    r = len(result)
    result += relocations
    s = len(result)
    result += symtab
    t = len(result)
    result += names
    n = len(result)
    result += SECTION_NAMES
    result += b"\0" * (align(len(result), 8) - len(result))
    sections = len(result)

    def sh(name, kind, flags, offset, size, link=0, info=0, alignment=1, entry=0):
        return struct.pack("<IIQQQQIIQQ", name, kind, flags, 0,
                           offset, size, link, info, alignment, entry)

    result += bytes(64)
    result += sh(1, 1, 6, 64, len(code), alignment=16)
    result += sh(7, 4, 0, r, len(relocations), link=3, info=1,
                 alignment=8, entry=24)
    result += sh(18, 2, 0, s, len(symtab), link=4, info=1,
                 alignment=8, entry=24)
    result += sh(26, 3, 0, t, len(names))
    result += sh(34, 3, 0, n, len(SECTION_NAMES))
    result += sh(44, 1, 0, n + len(SECTION_NAMES), 0)

    elf_id = b"\x7fELF" + bytes((2, 1, 1, 0)) + bytes(8)
    struct.pack_into("<16sHHIQQQIHHHHHH", result, 0, elf_id, 1, 62, 1,
                     0, 0, sections, 0, 64, 0, 0, 64, 7, 5)
    assert len(result) == sections + 448
    return bytes(result)


def packed(*objects):
    return b"".join(struct.pack("<I", len(obj)) + obj for obj in objects)


def parse_rel(data):
    """Independently inspect emitted symbol names, offsets, and relocations."""
    assert data[:7] == b"\x7fELF\x02\x01\x01"
    assert struct.unpack_from("<HH", data, 16) == (1, 62)
    sections = struct.unpack_from("<Q", data, 40)[0]
    assert struct.unpack_from("<HHH", data, 58) == (64, 7, 5)
    assert len(data) >= sections + 448

    def section(index):
        offset = sections + index * 64
        kind = struct.unpack_from("<I", data, offset + 4)[0]
        start, size = struct.unpack_from("<QQ", data, offset + 24)
        assert start + size <= len(data)
        return kind, data[start:start + size]

    assert section(1)[0] == 1
    assert section(2)[0] == 4
    assert section(3)[0] == 2
    strings = section(4)[1]
    symbols = []
    blob = section(3)[1]
    assert len(blob) % 24 == 0
    for offset in range(24, len(blob), 24):
        index, info, _, where, value, size = struct.unpack_from("<IBBHQQ", blob, offset)
        end = strings.index(b"\0", index)
        assert info == 18 and size == 0
        symbols.append((strings[index:end], where, value))
    rels = []
    blob = section(2)[1]
    assert len(blob) % 24 == 0
    for offset in range(0, len(blob), 24):
        position, info, addend = struct.unpack_from("<QQq", blob, offset)
        sym_index, kind = info >> 32, info & 0xffffffff
        assert kind == 4 and addend == -4 and 0 < sym_index <= len(symbols)
        rels.append((position, symbols[sym_index - 1][0]))
    return symbols, rels, section(1)[1]


def run(harness, archive, destination, mode, directory, label, success,
        failure_code=1):
    source = directory / (label + ".archive")
    source.write_bytes(archive)
    sentinel = b"original file must survive a failed merge\n"
    destination.write_bytes(sentinel)
    result = subprocess.run([str(harness), str(source), str(destination), str(mode)],
                            env=dict(os.environ, VPP_NATIVE_ELF_LINK="1"),
                            capture_output=True, timeout=120)
    (directory / (label + ".log")).write_bytes(result.stdout + result.stderr)
    assert result.returncode == (0 if success else failure_code), (
        label, result.returncode, result.stderr.decode(errors="replace")[-1000:])
    assert success or destination.read_bytes() == sentinel, label
    assert not list(directory.glob(destination.name + ".vpp-exe-*")), label
    print("NATIVE_ELF_REL_" + label.upper().replace("-", "_") + "=PASS", flush=True)


def execute(executable_harness, obj, directory, label, success=True):
    archive = directory / (label + ".archive")
    archive.write_bytes(packed(obj))
    executable = directory / (label + ".exe")
    # The production host has unresolved V++ runtime imports. These fixtures
    # intentionally define only main/foo, so give the final linker an isolated
    # ET_REL host with _start -> main -> Linux exit(60). Construct it using the
    # same ELF byte writer as the test objects: no assembler/linker is invoked.
    host_root = directory / "minimal-host"
    host_path = host_root / "scripts/ASM/linux/vpp_aot_primitives_linux_x86_64.o"
    host_path.parent.mkdir(parents=True, exist_ok=True)
    host_object = elf_object(
        bytes.fromhex("e80000000089c7b83c0000000f05"),
        [(b"_start", 0)], [b"main"], [(1, b"main")])
    assert parse_rel(host_object)[1] == [(1, b"main")]
    host_path.write_bytes(host_object)
    sentinel = b"original executable must survive a failed link\n"
    executable.write_bytes(sentinel)
    result = subprocess.run([str(executable_harness), str(archive), str(executable)],
                            env=dict(os.environ, VPP_NATIVE_ELF_LINK="1",
                                     VPP_HOME=str(host_root)),
                            capture_output=True, timeout=120)
    (directory / (label + ".log")).write_bytes(result.stdout + result.stderr)
    assert result.returncode == (0 if success else 1), (
        label, result.returncode, result.stderr[-1000:])
    if success:
        assert subprocess.run([str(executable)], timeout=30).returncode == 42, label
    else:
        assert executable.read_bytes() == sentinel, label
    assert not list(directory.glob(executable.name + ".vpp-exe-*")), label
    suffix = "_EXIT=42" if success else "_REJECTED=PASS"
    print("NATIVE_ELF_REL_" + label.upper().replace("-", "_") + suffix, flush=True)


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--self-test":
        harness = executable_harness = None
        directory = Path(sys.argv[2]).resolve()
    elif len(sys.argv) == 4:
        harness, executable_harness = (Path(path).resolve() for path in sys.argv[1:3])
        directory = Path(sys.argv[3]).resolve()
    else:
        raise SystemExit(__doc__)
    directory.mkdir(parents=True, exist_ok=True)

    # call foo; ret; relocation is at byte 1 of main.
    caller = elf_object(b"\xe8\0\0\0\0\xc3", [(b"main", 0)],
                        [b"foo"], [(1, b"foo")])
    # mov eax, 42; ret. The generated function uses no runtime imports.
    callee = elf_object(b"\xb8\x2a\0\0\0\xc3", [(b"foo", 0)])
    second_callee = elf_object(b"\xb8\x13\0\0\0\xc3", [(b"foo", 0)])
    malformed = bytearray(caller)
    rela_offset = struct.unpack_from("<Q", caller,
                                     struct.unpack_from("<Q", caller, 40)[0] + 128 + 24)[0]
    struct.pack_into("<I", malformed, rela_offset + 12, 32767)
    for obj in (caller, callee, second_callee):
        parse_rel(obj)

    if harness is None:
        (directory / "caller.o").write_bytes(caller)
        (directory / "callee.o").write_bytes(callee)
        print("NATIVE_ELF_REL_FIXTURE_GENERATION=PASS")
        return

    output = directory / "merged.o"
    run(harness, packed(caller, callee), output, 2, directory, "forward-definition", True)
    symbols, relocations, code = parse_rel(output.read_bytes())
    assert dict((name, (where, value)) for name, where, value in symbols) == {
        b"main": (1, 0), b"foo": (1, 16)}
    assert relocations == [(1, b"foo")]
    assert code[:6] == b"\xe8\0\0\0\0\xc3" and code[16:22] == callee[64:70]

    # The merged object must itself be consumable by the native ET_EXEC linker.
    execute(executable_harness, output.read_bytes(), directory, "merged-executable")
    # A partial object may legitimately still import foo. The final link
    # must reject that import and leave an existing destination untouched.
    execute(executable_harness, caller, directory, "unresolved-executable", False)

    # A canonical object may have an empty .rela.text section: verify that
    # both the partial and final linkers accept that boundary case.
    standalone = elf_object(b"\xb8\x2a\0\0\0\xc3", [(b"main", 0)])
    standalone_output = directory / "no-relocations.o"
    run(harness, packed(standalone), standalone_output, 2, directory,
        "no-relocations", True)
    symbols, relocations, _ = parse_rel(standalone_output.read_bytes())
    assert symbols == [(b"main", 1, 0)] and relocations == []
    execute(executable_harness, standalone_output.read_bytes(), directory,
            "no-relocations-executable")

    # A full multi-platform bootstrap may produce objects larger than the
    # former 1 MiB archive limit. Verify the real V++ partial/final linkers
    # consume an archive above that boundary and preserve executable code.
    large_code = b"\xb8\x2a\0\0\0\xc3" + b"\x90" * (1048576 + 512 - 6)
    large_object = elf_object(large_code, [(b"main", 0)])
    assert len(large_object) > 1048576
    assert len(packed(large_object)) <= 2097152
    large_output = directory / "large-archive.o"
    run(harness, packed(large_object), large_output, 2, directory,
        "archive-over-1mib", True)
    assert parse_rel(large_output.read_bytes())[2] == large_code
    execute(executable_harness, large_output.read_bytes(), directory,
            "archive-over-1mib-executable")
    print("NATIVE_ELF_ARCHIVE_OVER_1MIB=PASS", flush=True)

    # Two canonical objects make the packed length exactly divisible by 8.
    # At the arena limit the reader must probe EOF without appending another
    # block. The merged ET_REL is smaller because it removes one ELF header.
    tiny = elf_object(b"\xc3", [(b"foo", 0)])
    exact_code_size = 2097152 - len(packed(standalone, tiny)) + 8
    exact_object = elf_object(b"\xc3" + b"\x90" * (exact_code_size - 1),
                              [(b"foo", 0)])
    exact_archive = packed(standalone, exact_object)
    assert len(exact_archive) == 2097152
    exact_output = directory / "exact-2mib.o"
    run(harness, exact_archive, exact_output, 2, directory,
        "archive-exact-2mib", True)
    symbols, relocations, code = parse_rel(exact_output.read_bytes())
    assert symbols == [(b"main", 1, 0), (b"foo", 1, 16)]
    assert not relocations and code[16:] == exact_object[64:64 + exact_code_size]
    # An extra byte must be rejected by the file reader, before either linker
    # can publish an output. Its exit=2 distinguishes read failure from a
    # malformed archive rejected by the partial linker (exit=1).
    run(harness, exact_archive + b"\0", exact_output, 2, directory,
        "archive-over-2mib", False, failure_code=2)
    run(harness, exact_archive[:-1], exact_output, 2, directory,
        "archive-near-limit-truncated", False)

    reverse = directory / "reverse.o"
    run(harness, packed(callee, caller), reverse, 2, directory, "reverse-definition", True)
    symbols, relocations, _ = parse_rel(reverse.read_bytes())
    assert dict((name, value) for name, where, value in symbols if where == 1) == {
        b"foo": 0, b"main": 16}
    assert relocations == [(17, b"foo")]

    skipped = directory / "skipped.o"
    run(harness, packed(callee, caller), skipped, 1, directory, "skip-first-runtime", True)
    symbols, relocations, _ = parse_rel(skipped.read_bytes())
    assert (b"foo", 0, 0) in symbols and (b"main", 1, 0) in symbols
    assert relocations == [(1, b"foo")]

    # A two-stage partial link must preserve unresolved imports for a later
    # stage, and converge byte-for-byte with a one-shot three-object link.
    top = elf_object(b"\xe8\0\0\0\0\xc3", [(b"main", 0)],
                     [b"middle"], [(1, b"middle")])
    middle = elf_object(b"\xe8\0\0\0\0\xc3", [(b"middle", 0)],
                        [b"foo"], [(1, b"foo")])
    stage1 = directory / "chain-stage1.o"
    run(harness, packed(top, middle), stage1, 2, directory, "chain-stage1", True)
    symbols, relocations, _ = parse_rel(stage1.read_bytes())
    assert dict((name, (where, value)) for name, where, value in symbols) == {
        b"main": (1, 0), b"middle": (1, 16), b"foo": (0, 0)}
    assert relocations == [(1, b"middle"), (17, b"foo")]

    stage2 = directory / "chain-stage2.o"
    run(harness, packed(stage1.read_bytes(), callee), stage2, 2,
        directory, "chain-stage2", True)
    symbols, relocations, _ = parse_rel(stage2.read_bytes())
    assert dict((name, (where, value)) for name, where, value in symbols) == {
        b"main": (1, 0), b"middle": (1, 16), b"foo": (1, 32)}
    assert relocations == [(1, b"middle"), (17, b"foo")]
    execute(executable_harness, stage2.read_bytes(), directory, "chain-executable")
    direct = directory / "chain-direct.o"
    run(harness, packed(top, middle, callee), direct, 2,
        directory, "chain-direct", True)
    assert stage2.read_bytes() == direct.read_bytes(), "partial link stages diverged"
    print("NATIVE_ELF_REL_STAGE_CONVERGENCE=PASS", flush=True)

    def corrupt_section(obj, index, field, fmt, value):
        result = bytearray(obj)
        table = struct.unpack_from("<Q", result, 40)[0]
        struct.pack_into(fmt, result, table + index * 64 + field, value)
        return bytes(result)

    def corrupt_byte_range(obj, position, fmt, value):
        result = bytearray(obj)
        struct.pack_into(fmt, result, position, value)
        return bytes(result)

    # All seven section headers, including metadata and the empty stack
    # marker, must have a zero sh_addr in canonical ET_REL objects. Use a
    # self-contained main for both link paths so an unrelated unresolved
    # symbol cannot make the final-link rejection test pass accidentally.
    address_negatives = {
        label: corrupt_section(standalone, index, 16, "<I", 1)
        for label, index in (
            ("text-sh-addr-nonzero", 1),
            ("rela-sh-addr-nonzero", 2),
            ("symtab-sh-addr-nonzero", 3),
            ("strtab-sh-addr-nonzero", 4),
            ("shstrtab-sh-addr-nonzero", 5),
            ("gnu-stack-sh-addr-nonzero", 6),
        )
    }
    address_negatives["sh-addr-high-word"] = corrupt_section(
        standalone, 4, 20, "<I", 1)
    # Canonical section metadata has fixed flags, link/info, alignment and
    # entry sizes. Probe fields that the ELF loader must not silently ignore.
    metadata_negatives = {
        "null-section-nonzero": corrupt_section(standalone, 0, 16, "<I", 1),
        "strtab-link-nonzero": corrupt_section(standalone, 4, 40, "<I", 1),
        "strtab-info-nonzero": corrupt_section(standalone, 4, 44, "<I", 1),
        "symtab-info-invalid": corrupt_section(standalone, 3, 44, "<I", 0),
        "rela-alignment-invalid": corrupt_section(standalone, 2, 48, "<I", 1),
        "strtab-alignment-invalid": corrupt_section(standalone, 4, 48, "<I", 8),
        "gnu-stack-alignment-invalid": corrupt_section(standalone, 6, 48, "<I", 8),
        "strtab-entsize-nonzero": corrupt_section(standalone, 4, 56, "<I", 1),
        "rela-entsize-high-word": corrupt_section(standalone, 2, 60, "<I", 1),
    }
    # ET_REL files cannot carry an executable entry point or an ELF program
    # table, even when the section table is otherwise canonical. The old
    # linker silently accepted these corrupted headers and published output.
    header_negatives = {
        "elf-ident-abiversion": corrupt_byte_range(standalone, 9, "<B", 1),
        "elf-ident-padding": corrupt_byte_range(standalone, 15, "<B", 1),
        "elf-entry-low": corrupt_byte_range(standalone, 24, "<I", 4096),
        "elf-entry-high": corrupt_byte_range(standalone, 28, "<I", 1),
        "elf-phoff-low": corrupt_byte_range(standalone, 32, "<I", 64),
        "elf-phoff-high": corrupt_byte_range(standalone, 36, "<I", 1),
        "elf-flags": corrupt_byte_range(standalone, 48, "<I", 1),
        "elf-phentsize": corrupt_byte_range(standalone, 54, "<H", 56),
        "elf-phnum": corrupt_byte_range(standalone, 56, "<H", 1),
        "elf-shstrndx-past-end": corrupt_byte_range(standalone, 62, "<H", 127),
        "elf-shstrndx-points-to-code": corrupt_byte_range(standalone, 62, "<H", 1),
        "elf-shstrndx-points-to-rela": corrupt_byte_range(standalone, 62, "<H", 2),
        "elf-shstrndx-points-to-symtab": corrupt_byte_range(standalone, 62, "<H", 3),
        "elf-shstrtab-wrong-type": corrupt_section(standalone, 5, 4, "<I", 1),
        "elf-shstrtab-offset-past-end": corrupt_section(
            standalone, 5, 24, "<Q", len(standalone) - 1),
        "elf-shstrtab-size-high-word": corrupt_section(standalone, 5, 36, "<I", 1),
        "elf-shstrtab-missing-leading-nul": corrupt_byte_range(
            standalone, struct.unpack_from("<Q", standalone,
                struct.unpack_from("<Q", standalone, 40)[0] + 5 * 64 + 24)[0], "<B", 65),
        "elf-shstrtab-missing-trailing-nul": corrupt_byte_range(
            standalone, struct.unpack_from("<Q", standalone,
                struct.unpack_from("<Q", standalone, 40)[0] + 5 * 64 + 24)[0]
            + len(SECTION_NAMES) - 1, "<B", 65),
        "elf-shstrtab-name-past-end": corrupt_section(
            standalone, 1, 0, "<I", len(SECTION_NAMES)),
    }

    invalid_name = bytearray(caller)
    string_table = struct.unpack_from("<Q", caller,
                                     struct.unpack_from("<Q", caller, 40)[0] + 4 * 64 + 24)[0]
    # Force the symbol's name to point at a final unterminated string byte.
    symbol_table = struct.unpack_from("<Q", caller,
                                     struct.unpack_from("<Q", caller, 40)[0] + 3 * 64 + 24)[0]
    string_size = struct.unpack_from("<Q", caller,
                                     struct.unpack_from("<Q", caller, 40)[0] + 4 * 64 + 32)[0]
    invalid_name[string_table + string_size - 1] = ord("X")
    struct.pack_into("<I", invalid_name, symbol_table + 24, string_size - 1)

    negatives = {
        "duplicate-strong": packed(callee, second_callee),
        "bad-relocation": packed(bytes(malformed), callee),
        "truncated-object": packed(caller, callee)[:-1],
        "too-many-objects": packed(*(elf_object(b"\xc3", [(f"fn{i}".encode(), 0)])
                                           for i in range(33))),
        "overlap-elf-header": packed(corrupt_section(caller, 1, 24, "<Q", 0)),
        "overlap-code-rela": packed(corrupt_section(caller, 2, 24, "<Q", 64)),
        "overlap-rela-symtab": packed(corrupt_section(caller, 3, 24, "<Q", 72)),
        "overlap-symtab-strtab": packed(corrupt_section(caller, 4, 24, "<Q", 72)),
        "overlap-strtab-names": packed(corrupt_section(caller, 5, 24, "<Q", 72)),
        "unterminated-name": packed(bytes(invalid_name)),
        "relocation-zero-symbol": packed(
            bytes(caller[:rela_offset + 12] + b"\0\0\0\0" + caller[rela_offset + 16:])),
        # The linker uses signed i32 offsets inside its arena. High halves of
        # ELF64 offsets, sizes, relocation positions and symbol values must
        # be rejected before any byte or section metadata is accessed.
        "section-table-high-word": packed(corrupt_byte_range(caller, 44, "<I", 1)),
        "section-table-past-end": packed(corrupt_byte_range(
            caller, 40, "<I", len(caller) - 448 + 1)),
        "text-empty": packed(corrupt_section(caller, 1, 32, "<Q", 0)),
        "text-size-high-word": packed(corrupt_section(caller, 1, 36, "<I", 1)),
        "rela-offset-high-word": packed(corrupt_section(caller, 2, 28, "<I", 1)),
        "rela-size-high-word": packed(corrupt_section(caller, 2, 36, "<I", 1)),
        "relocation-offset-high-word": packed(corrupt_byte_range(
            caller, rela_offset + 4, "<I", 1)),
        "relocation-symbol-past-end": packed(corrupt_byte_range(
            caller, rela_offset + 12, "<I", 1000)),
        "relocation-addend-changed": packed(corrupt_byte_range(
            caller, rela_offset + 16, "<q", -8)),
        "symbol-name-past-end": packed(corrupt_byte_range(
            caller, symbol_table + 24, "<I", string_size)),
        "symbol-value-high-word": packed(corrupt_byte_range(
            caller, symbol_table + 24 + 12, "<I", 1)),
        "symbol-value-past-text": packed(corrupt_byte_range(
            caller, symbol_table + 24 + 8, "<I", 6)),
        "undefined-symbol-nonzero-value": packed(corrupt_byte_range(
            caller, symbol_table + 48 + 8, "<I", 1)),
        "symbol-invalid-section": packed(corrupt_byte_range(
            caller, symbol_table + 24 + 6, "<H", 2)),
        "strtab-missing-leading-nul": packed(corrupt_byte_range(
            caller, string_table, "<B", 65)),
    }
    negatives.update({label: packed(obj) for label, obj in address_negatives.items()})
    negatives.update({label: packed(obj) for label, obj in metadata_negatives.items()})
    negatives.update({label: packed(obj) for label, obj in header_negatives.items()})
    for label, archive_content in negatives.items():
        run(harness, archive_content, directory / (label + ".o"), 2,
            directory, label, False)
    for label, obj in address_negatives.items():
        execute(executable_harness, obj, directory, label, False)
    for label, obj in header_negatives.items():
        execute(executable_harness, obj, directory, label, False)
    # ELF permits SHN_UNDEF when section names are omitted. The canonical
    # V++ partial linker requires index five, but the general final linker
    # must continue to accept host-style ET_REL without section names.
    absent_section_names = corrupt_byte_range(standalone, 62, "<H", 0)
    run(harness, packed(absent_section_names), directory / "shstrndx-undef.o",
        2, directory, "shstrndx-undef-canonical", False)
    execute(executable_harness, absent_section_names, directory,
            "shstrndx-undef-general", True)
    # The general final linker also accepts a preassembled host object, so
    # only enforce shared ELF invariants on that path; stricter V++ metadata
    # layout rules are exercised by the canonical partial linker above.
    for label in ("null-section-nonzero", "strtab-link-nonzero",
                  "strtab-info-nonzero", "rela-entsize-high-word"):
        execute(executable_harness, metadata_negatives[label], directory,
                label, False)
    print("NATIVE_ELF_REL_REGRESSIONS=PASS", flush=True)


if __name__ == "__main__":
    main()
