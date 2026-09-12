#!/usr/bin/env python3
"""Enforce a line-coverage threshold from an LCOV tracefile."""

from __future__ import annotations

import argparse
from pathlib import Path


def read_totals(path: Path) -> tuple[int, int]:
    found = 0
    hit = 0
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("LF:"):
            found += int(line[3:])
        elif line.startswith("LH:"):
            hit += int(line[3:])
    return found, hit


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("tracefile", type=Path)
    parser.add_argument("--minimum", type=float, default=45.0)
    args = parser.parse_args()

    found, hit = read_totals(args.tracefile)
    if found == 0:
        print("coverage: no instrumented lines were found")
        return 2

    percent = hit * 100.0 / found
    print(f"coverage: {hit}/{found} lines = {percent:.2f}% (minimum {args.minimum:.2f}%)")
    return 0 if percent >= args.minimum else 1


if __name__ == "__main__":
    raise SystemExit(main())
