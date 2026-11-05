#!/usr/bin/env python3
"""Reject corrupted platform objects without overwriting an existing executable.

Usage: check_native_runtime_linker_inputs.py macho|pe <compiler> <boundary> <out>
The V++ compiler performs every link; Python only mutates binary input records.
"""
from pathlib import Path
import os
import struct
import subprocess
import sys


def mutations(kind, original):
    def changed(offset, fmt, value):
        data = bytearray(original)
        struct.pack_into(fmt, data, offset, value)
        return bytes(data)

    yield "truncated", original[:19]
    yield "wrong-machine", changed(4 if kind == "macho" else 0, "<I" if kind == "macho" else "<H", 0)
    if kind == "macho":
        yield "wrong-file-type", changed(12, "<I", 2)
        yield "command-too-short", changed(36, "<I", 4)
        position = 32
        sections = []
        for _ in range(struct.unpack_from("<I", original, 16)[0]):
            command, size = struct.unpack_from("<II", original, position)
            if command == 25:
                count = struct.unpack_from("<I", original, position + 64)[0]
                sections.extend(position + 72 + i * 80 for i in range(count))
            elif command == 2:
                sym, ns, strings, _ = struct.unpack_from("<IIII", original, position + 8)
                yield "symbol-table-past-end", changed(position + 8, "<I", len(original))
                yield "string-table-past-end", changed(position + 16, "<I", len(original))
            position += size
        section = sections[0]
        yield "unknown-section", changed(section, "<B", ord("X"))
        yield "section-size-high-word", changed(section + 44, "<I", 1)
        relocation = struct.unpack_from("<I", original, section + 56)[0]
        assert struct.unpack_from("<I", original, section + 60)[0] > 0
        yield "relocation-past-section", changed(relocation, "<I", 0x7fffffff)
        yield "unsupported-relocation", changed(relocation + 7, "<B", 0x6d)
        yield "relocation-symbol-past-end", changed(relocation + 4, "<I", 0x2dffffff)
        # A library typo must be rejected before producing an executable,
        # rather than deferring the error to dyld on the next launch.
        name = original.index(b"_mmap\0")
        yield "unknown-library-import", changed(name + 1, "<B", ord("z"))
        definitions = [sym + 16 * i for i in range(ns) if original[sym + 16 * i + 4] == 15]
        yield "duplicate-boundary-definition", changed(definitions[1], "<I", struct.unpack_from("<I", original, definitions[0])[0])
        for i in range(struct.unpack_from("<I", original, section + 60)[0]):
            rel = relocation + i * 8
            if original[rel + 7] == 0x4c:
                at = struct.unpack_from("<I", original, rel)[0] + struct.unpack_from("<I", original, section + 48)[0]
                if original[at + 3] == 0x91:
                    yield "shifted-pageoff-add", changed(at + 2, "<B", original[at + 2] | 0x40)
                    break
        else:
            raise AssertionError("boundary has no PAGEOFF12 ADD relocation")
    else:
        yield "optional-header", changed(16, "<H", 240)
        yield "too-many-sections", changed(2, "<H", 17)
        yield "symbol-table-past-end", changed(8, "<I", len(original))
        yield "unknown-section", changed(20, "<B", ord("X"))
        yield "section-data-past-end", changed(40, "<I", len(original))
        yield "writable-code-section", changed(56, "<I", 0xe0500020)
        # These are individually recognized flags. The first used to pass
        # because the linker checked flags and names independently.
        yield "code-masquerading-as-data", changed(56, "<I", 0xc0400040)
        yield "data-masquerading-as-code", changed(20, "<8s", b".data\0\0\0")
        relocation = struct.unpack_from("<I", original, 44)[0]
        assert struct.unpack_from("<H", original, 52)[0] > 0
        yield "relocation-past-section", changed(relocation, "<I", 0x7fffffff)
        yield "unsupported-absolute-relocation", changed(relocation + 8, "<H", 1)
        yield "relocation-symbol-past-end", changed(relocation + 4, "<I", 0x7fffffff)
        name = original.index(b"VirtualAlloc\0")
        yield "unknown-library-import", changed(name, "<B", ord("X"))
        sym, ns = struct.unpack_from("<II", original, 8)
        definitions = [sym + 18 * i for i in range(ns)
                       if original[sym + 18 * i + 16] == 2
                       and struct.unpack_from("<h", original, sym + 18 * i + 12)[0] > 0]
        duplicate = bytearray(original)
        duplicate[definitions[1]:definitions[1] + 8] = original[definitions[0]:definitions[0] + 8]
        yield "duplicate-boundary-definition", bytes(duplicate)


def main():
    if len(sys.argv) != 5 or sys.argv[1] not in ("macho", "pe"):
        raise SystemExit(__doc__)
    kind = sys.argv[1]
    compiler, boundary, out = map(lambda p: Path(p).resolve(), sys.argv[2:])
    out.mkdir(parents=True, exist_ok=True)
    source = out / "runtime.vi"
    source.write_text("hàm chính() { trả về máy đặt quyền tệp mở(-1,448); };\n")
    destination = out / "preserved"
    sentinel = b"previous executable must survive a rejected platform object\n"
    env = dict(os.environ, VPP_HOME=str(Path(__file__).resolve().parents[2]),
               VPP_NATIVE_NO_CACHE="1", VPP_MACOS_DIRECT_LD="0")
    if kind == "macho":
        env.update(VPP_NATIVE_MACHO_EXE_LINK="1")
        env.pop("VPP_NATIVE_TARGET", None)
        key = "VPP_MACOS_RUNTIME_OBJECT"
    else:
        env.update(VPP_NATIVE_TARGET="windows-x86_64")
        key = "VPP_WINDOWS_RUNTIME_OBJECT"
    for name, data in mutations(kind, boundary.read_bytes()):
        fixture = out / (name + ".o")
        fixture.write_bytes(data)
        destination.write_bytes(sentinel)
        result = subprocess.run([str(compiler), "build", str(source), "-o", str(destination)],
                                env=env | {key: str(fixture)}, capture_output=True, timeout=180)
        (out / (name + ".log")).write_bytes(result.stdout + result.stderr)
        assert result.returncode == 17, (kind, name, result.returncode, result.stderr[-1000:])
        assert destination.read_bytes() == sentinel, (kind, name, "output overwritten")
        assert not list(out.glob("*.vpp-exe-*")) and not list(out.glob("*.vpp-module-*"))
        print(f"{kind.upper()}_RUNTIME_REJECT_{name.upper().replace('-', '_')}=PASS", flush=True)
    print(f"{kind.upper()}_RUNTIME_INPUT_REGRESSIONS=PASS", flush=True)


if __name__ == "__main__":
    main()
