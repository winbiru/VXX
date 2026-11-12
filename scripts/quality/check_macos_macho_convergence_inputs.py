#!/usr/bin/env python3
"""Regression test: only Apple-owned UUID and signed trailer may vary."""

from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory

from check_macos_macho_convergence import normalized


def load_fields(data):
    count = struct.unpack_from("<I", data, 16)[0]
    offset = 32
    fields = {}
    for _ in range(count):
        cmd, size = struct.unpack_from("<II", data, offset)
        if cmd == 0x1B:
            fields["uuid"] = offset + 8
        if cmd == 0x1D:
            fields["signature"] = offset + 8
        if cmd == 0x19 and data[offset + 8 : offset + 24].rstrip(b"\0") == b"__LINKEDIT":
            fields["linkedit_size"] = offset + 48
        offset += size
    assert len(fields) == 3
    return fields


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_macos_macho_convergence_inputs.py <signed-compiler>")
    original = bytearray(Path(sys.argv[1]).read_bytes())
    fields = load_fields(original)
    sig_off, sig_size = struct.unpack_from("<II", original, fields["signature"])
    reference = normalized(Path(sys.argv[1]))

    with TemporaryDirectory(prefix="vpp-macho-convergence-") as directory:
        target = Path(directory) / "mutated"

        def expect(blob, equal=None, invalid=False):
            target.write_bytes(blob)
            try:
                got = normalized(target)
            except ValueError:
                if invalid:
                    return
                raise
            assert not invalid, "accepted malformed Mach-O metadata"
            assert (got == reference) == equal, "unexpected convergence result"

        uuid_changed = original[:]
        uuid_changed[fields["uuid"]] ^= 1
        expect(uuid_changed, equal=True)

        # Apple's signature may gain an eight-byte alignment trailer even
        # when no instruction or imported symbol changes.
        longer_signature = original[:] + bytes(8)
        struct.pack_into("<I", longer_signature, fields["signature"] + 4, sig_size + 8)
        current = struct.unpack_from("<Q", original, fields["linkedit_size"])[0]
        struct.pack_into("<Q", longer_signature, fields["linkedit_size"], current + 8)
        expect(longer_signature, equal=True)

        mutated_code = original[:]
        # The first byte after all load commands belongs to executable code
        # or alignment; either way it must participate in the comparison.
        load_commands_end = 32 + struct.unpack_from("<I", original, 20)[0]
        assert load_commands_end < sig_off
        mutated_code[load_commands_end] ^= 1
        expect(mutated_code, equal=False)

        wrong_sig_size = original[:]
        struct.pack_into("<I", wrong_sig_size, fields["signature"] + 4, sig_size + 8)
        expect(wrong_sig_size, invalid=True)

        wrong_linkedit_size = original[:]
        struct.pack_into("<Q", wrong_linkedit_size, fields["linkedit_size"], current + 8)
        expect(wrong_linkedit_size, invalid=True)

        wrong_signature_magic = original[:]
        wrong_signature_magic[sig_off] ^= 1
        expect(wrong_signature_magic, invalid=True)

    print("MACOS_MACHO_CONVERGENCE_METADATA=PASS")


if __name__ == "__main__":
    main()
