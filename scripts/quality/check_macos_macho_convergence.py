#!/usr/bin/env python3
"""Check native arm64 compiler convergence, ignoring linker-owned Mach-O metadata.

Apple ld can assign a different LC_UUID on successive links. Its ad-hoc
signature then changes too, including the signature length, even when every
byte of the compiled image is identical. Normalize only these linker-owned
fields and the signature trailer, and reject inconsistent Mach-O boundaries.
"""

import hashlib
from pathlib import Path
import struct
import sys

MH_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C
LC_SEGMENT_64 = 0x19
LC_UUID = 0x1B
LC_CODE_SIGNATURE = 0x1D


def normalized(path: Path) -> bytes:
    data = bytearray(path.read_bytes())
    if len(data) < 32:
        raise ValueError(f"{path}: truncated Mach-O header")
    magic, cpu_type, _, _, ncmds, sizeofcmds, _, _ = struct.unpack_from("<8I", data)
    if magic != MH_MAGIC_64 or cpu_type != CPU_TYPE_ARM64:
        raise ValueError(f"{path}: expected native Mach-O ARM64")
    end = 32 + sizeofcmds
    if end > len(data):
        raise ValueError(f"{path}: truncated load commands")
    offset = 32
    uuid_ranges = []
    signatures = []
    linkedits = []
    for _ in range(ncmds):
        if offset + 8 > end:
            raise ValueError(f"{path}: truncated load command")
        cmd, size = struct.unpack_from("<2I", data, offset)
        if size < 8 or offset + size > end:
            raise ValueError(f"{path}: invalid load command length")
        if cmd == LC_UUID:
            if size != 24:
                raise ValueError(f"{path}: malformed LC_UUID")
            uuid_ranges.append((offset + 8, 16))
        if cmd == LC_CODE_SIGNATURE:
            if size != 16:
                raise ValueError(f"{path}: malformed LC_CODE_SIGNATURE")
            signatures.append((offset + 12, *struct.unpack_from("<2I", data, offset + 8)))
        if cmd == LC_SEGMENT_64:
            if size < 72:
                raise ValueError(f"{path}: malformed LC_SEGMENT_64")
            if data[offset + 8 : offset + 24].rstrip(b"\0") == b"__LINKEDIT":
                fileoff, filesize = struct.unpack_from("<QQ", data, offset + 40)
                linkedits.append((offset + 48, fileoff, filesize))
        offset += size
    if (offset != end or len(uuid_ranges) != 1 or len(signatures) != 1
            or len(linkedits) != 1):
        raise ValueError(f"{path}: unexpected Mach-O load commands")
    signature_size_field, signature_off, signature_size = signatures[0]
    linkedit_size_field, linkedit_off, linkedit_size = linkedits[0]
    if (signature_size == 0 or signature_off < end
            or signature_off < linkedit_off
            or signature_off + signature_size != len(data)
            or linkedit_off + linkedit_size != len(data)):
        raise ValueError(f"{path}: invalid signature or __LINKEDIT bounds")
    # Verify the signature trailer's SuperBlob header before disregarding it.
    if (signature_size < 12 or
            struct.unpack_from(">I", data, signature_off)[0] != 0xFADE0CC0 or
            struct.unpack_from(">I", data, signature_off + 4)[0] > signature_size):
        raise ValueError(f"{path}: invalid code signature SuperBlob")
    for start, size in uuid_ranges:
        data[start : start + size] = b"\x00" * size
    # The trailer may differ in length by alignment bytes (e.g. 5800 vs 5808).
    # Keep the signature offset and all preceding code/data byte-exact.
    struct.pack_into("<I", data, signature_size_field, 0)
    struct.pack_into("<Q", data, linkedit_size_field, signature_off - linkedit_off)
    return bytes(data[:signature_off])


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: check_macos_macho_convergence.py <stage-N> <stage-N+1>", file=sys.stderr)
        return 2
    try:
        left = normalized(Path(sys.argv[1]))
        right = normalized(Path(sys.argv[2]))
    except (OSError, ValueError, struct.error) as exc:
        print(f"MACOS_MACHO_CONVERGENCE=FAIL: {exc}", file=sys.stderr)
        return 1
    if left != right:
        print("MACOS_MACHO_CONVERGENCE=FAIL: compiled image differs", file=sys.stderr)
        return 1
    print(f"MACOS_MACHO_CONVERGENCE=PASS sha256={hashlib.sha256(left).hexdigest()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
