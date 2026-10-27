"""Verify native V++ diagnostics on unsaved LSP buffers and UTF-16 positions."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def packet(message):
    body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    return b"Content-Length: " + str(len(body)).encode("ascii") + b"\r\n\r\n" + body


def read_packets(data):
    messages = []
    while data:
        header, marker, payload = data.partition(b"\r\n\r\n")
        if not marker:
            raise AssertionError("missing LSP header")
        size = next(int(line.split(b":", 1)[1].strip())
                    for line in header.split(b"\r\n")
                    if line.lower().startswith(b"content-length:"))
        messages.append(json.loads(payload[:size].decode("utf-8")))
        data = payload[size:]
    return messages


def main():
    cli, compiler, root = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="vpp-lsp-native-") as directory:
        project = Path(directory)
        document = project / "mã thử.vi"
        document.write_text("hàm sai( {\n", encoding="utf-8")
        (project / "phụ trợ.vi").write_text("hàm hỗ trợ() { trả về 1; };\n", encoding="utf-8")
        uri = document.as_uri()
        valid = "nhập phụ trợ;\nhàm chính() { in 1; };\n"
        duplicate = 'in "🌿"; hàm f() {}; hàm f() {};\n'
        invalid_import = "nhập chưa tồn tại;\n"
        request = [
            {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
            {"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
                "textDocument": {"uri": uri, "languageId": "vpp", "version": 1, "text": valid}}},
            {"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {
                "textDocument": {"uri": uri, "version": 2},
                "contentChanges": [{"text": duplicate}]}},
            {"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {
                "textDocument": {"uri": uri, "version": 3},
                "contentChanges": [{"text": invalid_import}]}},
            {"jsonrpc": "2.0", "method": "textDocument/didClose", "params": {
                "textDocument": {"uri": uri}}},
            {"jsonrpc": "2.0", "id": 2, "method": "shutdown"},
            {"jsonrpc": "2.0", "method": "exit"},
        ]
        env = os.environ.copy()
        env["VPP_HOME"] = root
        env["VPP_NATIVE_COMPILER"] = compiler
        result = subprocess.run([cli, "--lsp"], input=b"".join(map(packet, request)),
                                capture_output=True, env=env, timeout=120, check=False)
        if result.returncode != 0:
            raise AssertionError(f"LSP exited {result.returncode}: {result.stderr!r}")
        messages = read_packets(result.stdout)
        published = [message["params"]["diagnostics"] for message in messages
                     if message.get("method") == "textDocument/publishDiagnostics"]
        if len(published) != 4:
            raise AssertionError(f"expected 4 diagnostic notifications: {published!r}")
        if published[0] or published[3]:
            raise AssertionError(f"unsaved valid source or didClose failed: {published!r}")
        if len(published[1]) != 1 or "trùng khai báo hàm" not in published[1][0]["message"]:
            raise AssertionError(f"native semantic diagnostics missing: {published[1]!r}")
        # Position of the second hàm: compare Unicode characters to UTF-16 units.
        second = duplicate.index("hàm", duplicate.index("hàm") + 1)
        utf16_offset = len(duplicate[:second].encode("utf-16-le")) // 2
        position = published[1][0]["range"]["start"]
        if position != {"line": 0, "character": utf16_offset}:
            raise AssertionError(f"incorrect UTF-16 diagnostic position: {position}, expected {utf16_offset}")
        if len(published[2]) != 1 or "không tìm thấy import" not in published[2][0]["message"]:
            raise AssertionError(f"native import resolution missing: {published[2]!r}")
        if document.read_text(encoding="utf-8") != "hàm sai( {\n":
            raise AssertionError("LSP lint modified the source on disk")
        print("Native LSP diagnostics: unsaved buffer, relative imports, UTF-16, close PASS")


if __name__ == "__main__":
    main()
