#!/usr/bin/env python3
"""Drive the actual V++ DNS mock server and standalone VM on Windows."""
import argparse
import os
from pathlib import Path
import sys

from run_native_regression import normalize, run_dns_mode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("server", "vm", "image"):
        parser.add_argument(name, type=Path)
    parser.add_argument("mode", type=int)
    parser.add_argument("queries", type=int)
    parser.add_argument("work", type=Path)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    run_dns_mode(args.server.resolve(), args.vm.resolve(), work, dict(os.environ),
                 work, args.mode, args.queries, image=args.image.resolve())
    if args.mode >= 10:
        sys.stdout.buffer.write(b"VPP DNS VM REJECTED\n")
    else:
        sys.stdout.buffer.write(normalize((work / "client.stdout").read_bytes()))


if __name__ == "__main__":
    main()
