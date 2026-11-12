#!/usr/bin/env python3
"""Map VPP_VM_PROFILE sample function IDs to names in a VPPVM image.

Usage: python3 scripts/quality/profile_vm_asm_hotspots.py image.vmb profile.log

Sampling happens every 250,000 guest instructions. Percentages therefore
estimate the share of *guest instructions*, not wall or CPU time.
"""

import argparse
from collections import Counter
from pathlib import Path
import re
import struct


SAMPLE = re.compile(
    r"vm-profile: steps=(\d+) function=(\d+) pc=(\d+) "
    r"allocations=(\d+) gc=(\d+) free-list-probes=(\d+)"
)


def read_function_names(path: Path) -> list[str]:
    image = path.read_bytes()
    if len(image) < 24 or image[:8] not in (b"VPPVM\x00\x01\x00", b"VPPVM\x00\x02\x00"):
        raise ValueError(f"not a supported VPPVM image: {path}")

    count_strings, count_functions, _, _ = struct.unpack_from("<4i", image, 8)
    if not (0 <= count_strings <= 1_000_000 and 0 <= count_functions <= 100_000):
        raise ValueError("invalid VPPVM image counts")

    offset = 24

    def read_i32() -> int:
        nonlocal offset
        value = struct.unpack_from("<i", image, offset)[0]
        offset += 4
        return value

    strings = []
    for _ in range(count_strings):
        size = read_i32()
        if size < 0 or offset + size > len(image):
            raise ValueError("invalid VPPVM string length")
        strings.append(image[offset : offset + size].decode("utf-8", errors="replace"))
        offset += size

    names = []
    for index in range(count_functions):
        function_id, name_index, instructions = read_i32(), read_i32(), read_i32()
        if function_id != index or instructions < 0 or offset + 16 * instructions > len(image):
            raise ValueError("invalid VPPVM function record")
        names.append(strings[name_index] if 0 <= name_index < count_strings else "<anonymous>")
        offset += 16 * instructions
    return names


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("log", type=Path)
    parser.add_argument("--top", type=int, default=20)
    args = parser.parse_args()

    names = read_function_names(args.image)
    counts: Counter[int] = Counter()
    steps = 0
    latest = None
    for line in args.log.open(encoding="utf-8", errors="replace"):
        match = SAMPLE.search(line)
        if match is None:
            continue
        guest_steps, function_id, _, *counters = map(int, match.groups())
        counts[function_id] += guest_steps
        steps += guest_steps
        latest = counters

    print(f"Samples: {sum(counts.values()) // 250000}; observed guest steps: {steps:,}")
    if latest:
        print(f"Last counters: allocations={latest[0]:,} gc={latest[1]:,} "
              f"free-list-probes={latest[2]:,}")
    print("Share of sampled guest instructions (not CPU time):")
    for function_id, observed in counts.most_common(max(args.top, 0)):
        name = names[function_id] if 0 <= function_id < len(names) else "<invalid ID>"
        print(f"{100 * observed / steps:5.1f}%  {observed:>11,}  #{function_id:<4} {name}")


if __name__ == "__main__":
    main()
