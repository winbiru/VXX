#!/usr/bin/env python3
"""Independently validate the COFF objects emitted by V++'s native compiler."""

from pathlib import Path
import struct
import sys


def check(path: Path) -> tuple[list[str], list[str]]:
    data = path.read_bytes()
    assert len(data) >= 64, path
    machine, sections, timestamp, symoff, symcount, optional, flags = struct.unpack_from(
        "<HHIIIHH", data
    )
    assert (machine, sections, timestamp, optional, flags) == (0x8664, 1, 0, 0, 0), path
    section = struct.unpack_from("<8sIIIIIIHHI", data, 20)
    name, _, address, size, codeoff, reloff, lines, relcount, linecount, characteristics = section
    assert name == b".text\0\0\0", path
    assert (address, codeoff, lines, linecount, characteristics) == (
        0, 60, 0, 0, 0x60500020
    ), path
    assert size > 0 and codeoff + size <= len(data), path
    assert reloff == codeoff + size and symoff == reloff + relcount * 10, path
    assert symcount > 0 and symoff + symcount * 18 + 4 <= len(data), path
    stringoff = symoff + symcount * 18
    stringsize = struct.unpack_from("<I", data, stringoff)[0]
    assert stringsize >= 4 and stringoff + stringsize == len(data), path
    strings = data[stringoff + 4:]

    symbols = []
    for i in range(symcount):
        entry = symoff + i * 18
        inline = data[entry:entry + 8]
        if inline[:4] == b"\0" * 4:
            nameoff = struct.unpack_from("<I", inline, 4)[0]
            assert 4 <= nameoff < stringsize, path
            start = nameoff - 4
            end = strings.find(b"\0", start)
            assert end > start, path
            symbol = strings[start:end].decode("utf-8")
        else:
            symbol = inline.split(b"\0", 1)[0].decode("utf-8")
        value, section_number, typ, storage, auxiliary = struct.unpack_from(
            "<IhHBB", data, entry + 8
        )
        assert symbol and auxiliary == 0 and typ == 0x20 and storage == 2, path
        assert section_number in (0, 1), path
        assert value < size if section_number == 1 else value == 0, path
        symbols.append(symbol)

    relocations = []
    for i in range(relcount):
        offset, target, kind = struct.unpack_from("<IIH", data, reloff + i * 10)
        assert offset <= size - 4 and target < symcount and kind == 4, path
        relocations.append(symbols[target])
    assert "main" in symbols, (path, symbols)
    return symbols, relocations


def main() -> None:
    assert len(sys.argv) > 1, "usage: check_windows_coff_objects.py <name.obj>..."
    for filename in sys.argv[1:]:
        path = Path(filename)
        symbols, relocs = check(path)
        if path.stem == "runtime":
            assert "vpp_native_os_fchmod" in relocs, (path, relocs)
        if path.stem == "multi":
            assert "vpp_fn_phụ" in relocs, (path, relocs)
        if path.stem == "complex":
            assert "vpp_fn_hàm_đặc_biệt_dài" in symbols, (path, symbols)
            assert "vpp_fn_phần_gọi_runtime" in symbols, (path, symbols)
            assert relocs.count("vpp_fn_phụ_số") == 2, (path, relocs)
            assert relocs.count("vpp_fn_hàm_đặc_biệt_dài") == 2, (path, relocs)
            assert "vpp_native_os_fchmod" in relocs, (path, relocs)
        if path.stem in ("abi4", "abi8", "branch"):
            assert len(symbols) >= 1, path
        print(f"WINDOWS_COFF_{path.stem.upper()}=PASS symbols={len(symbols)} relocations={len(relocs)}")


if __name__ == "__main__":
    main()
