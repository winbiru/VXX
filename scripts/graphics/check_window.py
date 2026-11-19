"""Create a tiny BMP and exercise the native SDL rendering path."""
import json
import pathlib
import struct
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="vpp-graphics-") as work:
    path = pathlib.Path(work) / "ảnh thử.bmp"
    pixels = bytes((220, 120, 30, 0))  # BMP24 BGR + row padding
    header = struct.pack("<2sIHHI", b"BM", 54 + len(pixels), 0, 0, 54)
    dib = struct.pack("<IiiHHIIiiII", 40, 1, 1, 1, 24, 0, len(pixels), 0, 0, 0, 0)
    path.write_bytes(header + dib + pixels)
    subprocess.run([sys.argv[1], "--window-check", str(path), "V++ đồ họa", "64", "64"],
                   check=True, timeout=15)
    invalid = subprocess.run([sys.argv[1], "--window-check", str(path), "V++", "64abc", "64"],
                             timeout=15)
    assert invalid.returncode != 0
    events = subprocess.run([sys.argv[1], "--window-events-check", str(path), "V++ đồ họa", "64", "64"],
                            check=True, capture_output=True, encoding="utf-8", timeout=15)
    received = [json.loads(line) for line in events.stdout.splitlines()]
    assert {"loại": "bàn phím", "phím": "97", "mã phím": 97, "đang nhấn": True} in received, received
    assert {"loại": "chuột", "x": 4, "y": 5, "nút": 0, "đang nhấn": True} in received, received
