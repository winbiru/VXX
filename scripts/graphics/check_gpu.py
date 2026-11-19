"""Verify real GPU readback and reject invalid helper input."""
import json
import subprocess
import sys

helper, command = sys.argv[1:]
for invalid in ["256", "-1", "12abc", "4294967296", "-4294967296"]:
    result = subprocess.run([helper, command, invalid, "0", "0"], timeout=5)
    assert result.returncode != 0, invalid
for color in [(30, 120, 220), (0, 255, 128)]:
    result = subprocess.run([helper, command, *map(str, color)], check=True,
                            capture_output=True, encoding="utf-8", timeout=20)
    assert json.loads(result.stdout) == dict(zip(("đỏ", "lục", "lam", "alpha"), (*color, 255))), result.stdout
