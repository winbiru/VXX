#!/usr/bin/env python3
"""Exercise the canonical Mach-O ARM64 merger through its native V++ entrypoint.

The objects are constructed independently of the V++ compiler so corrupt
headers, symbol entries and relocations can be tested reproducibly.
"""
from pathlib import Path
import struct
import subprocess
import sys


def align8(n):
    return (n + 7) & ~7


def object_file(name, code, definitions=True, calls=()):
    strings = b"\0" + name + b"\0"
    symbols = struct.pack("<IBBHQ", 1, 15 if definitions else 1,
                          1 if definitions else 0, 0, 0)
    relocations = b"".join(struct.pack("<II", pos, 0x2d000000)
                           for pos in calls)
    rel_off = align8(232 + len(code))
    sym_off = rel_off + len(relocations)
    str_off = sym_off + len(symbols)
    header = struct.pack("<IiiIIIII", 0xfeedfacf, 16777228, 0,
                         1, 3, 200, 0, 0)
    segment = struct.pack("<II16sQQQQiiII", 25, 152, bytes(16),
                          0, len(code), 232, len(code), 7, 5, 1, 0)
    section = struct.pack("<16s16sQQIIIIIIII", b"__text", b"__TEXT",
                          0, len(code), 232, 2,
                          rel_off if relocations else 0, len(calls),
                          0x80000400, 0, 0, 0)
    symtab = struct.pack("<IIIIII", 2, 24, sym_off, 1, str_off, len(strings))
    build = struct.pack("<IIIIII", 50, 24, 1, 720896, 720896, 0)
    result = header + segment + section + symtab + build
    assert len(result) == 232
    result += code + bytes(rel_off - 232 - len(code))
    result += relocations + symbols + strings
    return result


def archive(*objs):
    return b"".join(struct.pack("<I", len(obj)) + obj for obj in objs)


def modify(blob, offset, value):
    result = bytearray(blob)
    struct.pack_into("<I", result, offset, value)
    return bytes(result)


def modify_byte(blob, offset, value):
    result = bytearray(blob)
    result[offset] = value
    return bytes(result)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: check_macos_native_macho_inputs.py <V++-harness> <output-dir>")
    harness, directory = Path(sys.argv[1]).resolve(), Path(sys.argv[2])
    directory.mkdir(parents=True, exist_ok=True)

    # mov w0, #53; ret. The second module calls this helper using BRANCH26.
    helper = object_file("_vpp_fn_phụ".encode(), struct.pack("<II", 0x528006a0, 0xd65f03c0))
    # Preserve LR across BL; otherwise the final ret branches to itself.
    main_code = struct.pack("<IIIII", 0xa9bf7bfd, 0x910003fd, 0x94000000,
                            0xa8c17bfd, 0xd65f03c0)
    main_obj = object_file(b"_main", main_code)
    # Add an undefined helper entry and point the BL relocation at symbol #1.
    names = "\0_main\0_vpp_fn_phụ\0".encode()
    rel_off = align8(232 + len(main_code))
    sym_off = rel_off + 8
    symbols = (struct.pack("<IBBHQ", 1, 15, 1, 0, 0) +
               struct.pack("<IBBHQ", 7, 1, 0, 0, 0))
    main_obj = bytearray(main_obj[:rel_off])
    struct.pack_into("<I", main_obj, 160, rel_off)
    struct.pack_into("<I", main_obj, 164, 1)
    struct.pack_into("<I", main_obj, 192, sym_off)
    struct.pack_into("<I", main_obj, 196, 2)
    struct.pack_into("<I", main_obj, 200, sym_off + len(symbols))
    struct.pack_into("<I", main_obj, 204, len(names))
    main_obj += struct.pack("<II", 8, 0x2d000001) + symbols + names
    main_obj = bytes(main_obj)

    def run(label, data, should_pass):
        src, dst = directory / (label + ".archive"), directory / (label + ".o")
        src.write_bytes(data)
        sentinel = b"prior output must survive an invalid Mach-O archive\n"
        dst.write_bytes(sentinel)
        result = subprocess.run([str(harness), str(src), str(dst)],
                                capture_output=True, timeout=60)
        if should_pass:
            assert result.returncode == 0, (label, result.stderr.decode(errors="replace"))
            assert dst.read_bytes().startswith(bytes.fromhex("cffaedfe")), label
        else:
            assert result.returncode == 1, (label, result.returncode, result.stderr)
            assert dst.read_bytes() == sentinel, label
        return dst

    merged = run("good", archive(helper, main_obj), True)
    repeat = run("good-repeat", archive(helper, main_obj), True)
    assert merged.read_bytes() == repeat.read_bytes()
    # Internal imports must use the definition's symbol index after merging.
    resolved = merged.read_bytes()
    assert struct.unpack_from("<I", resolved, 196)[0] == 2
    rel = struct.unpack_from("<I", resolved, 160)[0]
    assert struct.unpack_from("<I", resolved, rel + 4)[0] == 0x2D000000
    reversed_merge = run("definition-after-import", archive(main_obj, helper), True)
    reversed_bytes = reversed_merge.read_bytes()
    assert struct.unpack_from("<I", reversed_bytes, 196)[0] == 2
    reversed_rel = struct.unpack_from("<I", reversed_bytes, 160)[0]
    assert struct.unpack_from("<I", reversed_bytes, reversed_rel + 4)[0] == 0x2D000001
    # Two separate source units import the same unresolved external symbol.
    # Both relocations must target its single surviving undefined entry.
    other_main = main_obj.replace(b"_main\0", b"_aux_\0", 1)
    unresolved = run("shared-unresolved", archive(main_obj, other_main), True).read_bytes()
    assert struct.unpack_from("<I", unresolved, 196)[0] == 3
    unresolved_rel = struct.unpack_from("<I", unresolved, 160)[0]
    assert struct.unpack_from("<I", unresolved, unresolved_rel + 4)[0] == 0x2D000002
    assert struct.unpack_from("<I", unresolved, unresolved_rel + 12)[0] == 0x2D000002
    # Matching prefixes must not be mistaken for duplicate symbol names.
    prefix = object_file("_vpp_fn_phụ_extra".encode(), struct.pack("<II", 0x528006a0, 0xd65f03c0))
    run("distinct-prefix", archive(helper, prefix), True)
    # The compiler's byte runtime has a bounded handle table. A merger must
    # process many source units without allocating section-name literals on
    # every header byte check.
    many = [object_file(f"_vpp_unit_{i}".encode(),
                        struct.pack("<I", 0xd65f03c0)) for i in range(12)]
    many_result = run("many-source-units", archive(*many), True).read_bytes()
    assert struct.unpack_from("<I", many_result, 196)[0] == len(many)

    helper_sym = struct.unpack_from("<I", helper, 192)[0]
    helper_str = struct.unpack_from("<I", helper, 200)[0]

    bad_cases = {
        "empty": b"",
        "truncated-archive-length": struct.pack("<I", len(helper) + 1) + helper,
        "truncated-object": archive(helper[:-1]),
        "bad-magic": archive(modify(helper, 0, 0)),
        "bad-cpu": archive(modify(helper, 4, 0)),
        "bad-cpu-subtype": archive(modify(helper, 8, 1)),
        "bad-fileoff": archive(modify(helper, 72, 0)),
        "bad-segment-size": archive(modify(helper, 64, 7)),
        "bad-section-size": archive(modify(helper, 144, 7)),
        "bad-section-flags": archive(modify(helper, 168, 0)),
        "bad-segment-protections": archive(modify(helper, 88, 0)),
        "bad-segment-name": archive(modify_byte(helper, 40, ord("X"))),
        "bad-section-name": archive(modify_byte(helper, 104, ord("X"))),
        "bad-section-segment-name": archive(modify_byte(helper, 120, ord("X"))),
        "bad-section-name-padding": archive(modify_byte(helper, 111, ord("X"))),
        "bad-section-size-high": archive(modify(helper, 148, 1)),
        "bad-section-address-high": archive(modify(helper, 140, 1)),
        "bad-section-reserved": archive(modify(helper, 172, 1)),
        "bad-build-platform": archive(modify(helper, 216, 2)),
        "bad-build-minos": archive(modify(helper, 220, 0)),
        "bad-build-sdk": archive(modify(helper, 224, 0)),
        "bad-symbol-index": archive(modify(main_obj, rel_off + 4, 0x2d000002)),
        "bad-relocation-type": archive(modify(main_obj, rel_off + 4, 0x2c000001)),
        "bad-relocation-offset": archive(modify(main_obj, rel_off, len(main_code))),
        "bad-string-index": archive(modify(helper, struct.unpack_from("<I", helper, 192)[0], 999)),
        "interior-string-index": archive(modify(helper, helper_sym, 2)),
        "terminator-string-index": archive(modify(helper, helper_sym, len(helper) - helper_str - 1)),
        "bad-nlist-description": archive(modify_byte(helper, helper_sym + 6, 1)),
        "bad-string-size": archive(modify(helper, 204, 1)),
        "unterminated-symbol-name": archive(helper[:-1] + b"A"),
        "empty-symbol-name": archive(modify(helper, struct.unpack_from("<I", helper, 192)[0], 0)),
        "wrong-padding-after-code": archive(modify_byte(main_obj, 232 + len(main_code), 1)),
        "duplicate-definition": archive(helper, helper),
        "duplicate-main": archive(main_obj, main_obj),
    }
    for label, data in bad_cases.items():
        run(label, data, False)
    assert not list(directory.glob("*.vpp-module-*"))
    print("MACOS_NATIVE_MACHO_MALFORMED=PASS")
    print("MACOS_NATIVE_MACHO_DUPLICATE_DEFINITIONS=PASS")
    print("MACOS_NATIVE_MACHO_ATOMIC_FAILURE=PASS")
    print("MACOS_NATIVE_MACHO_SYNTHETIC_DETERMINISM=PASS")
    print("MACOS_NATIVE_MACHO_SYMBOL_RESOLUTION=PASS")
    print("MACOS_NATIVE_MACHO_UNDEFINED_DEDUP=PASS")
    print("MACOS_NATIVE_MACHO_MANY_SOURCE_UNITS=PASS")


if __name__ == "__main__":
    main()
