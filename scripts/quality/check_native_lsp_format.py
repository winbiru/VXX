"""Verify LSP formatting uses the bootstrapped V++ compiler for unsaved buffers."""

import json
import os
import subprocess
import sys


def packet(message):
    body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    return b"Content-Length: " + str(len(body)).encode("ascii") + b"\r\n\r\n" + body


def read_packets(data):
    messages = []
    while data:
        header, marker, payload = data.partition(b"\r\n\r\n")
        if not marker:
            raise AssertionError("LSP frame is missing header separator")
        lengths = [line.split(b":", 1)[1].strip() for line in header.split(b"\r\n")
                   if line.lower().startswith(b"content-length:")]
        if len(lengths) != 1:
            raise AssertionError("LSP frame has invalid Content-Length")
        size = int(lengths[0])
        messages.append(json.loads(payload[:size].decode("utf-8")))
        data = payload[size:]
    return messages


def main():
    cli, compiler, root = sys.argv[1:]
    uri = "file:///private/tmp/vpp-lsp-unsaved-buffer.vi"
    source = 'hàm chính(){in "chào 🌿";};'
    bad_source = 'in "chưa đóng;'
    request = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
            "textDocument": {"uri": uri, "languageId": "vpp", "version": 1, "text": source}}},
        {"jsonrpc": "2.0", "id": 2, "method": "textDocument/formatting", "params": {
            "textDocument": {"uri": uri}, "options": {"tabSize": 4, "insertSpaces": True}}},
        {"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {
            "textDocument": {"uri": uri, "version": 2}, "contentChanges": [{"text": bad_source}]}},
        {"jsonrpc": "2.0", "id": 3, "method": "textDocument/formatting", "params": {
            "textDocument": {"uri": uri}, "options": {"tabSize": 4, "insertSpaces": True}}},
        {"jsonrpc": "2.0", "id": 4, "method": "shutdown"},
        {"jsonrpc": "2.0", "method": "exit"},
    ]
    environment = os.environ.copy()
    environment["VPP_HOME"] = root
    environment["VPP_NATIVE_COMPILER"] = compiler
    result = subprocess.run([cli, "--lsp"], input=b"".join(map(packet, request)),
                            capture_output=True, env=environment, timeout=90, check=False)
    if result.returncode != 0:
        raise AssertionError(f"LSP exited {result.returncode}: {result.stderr!r}")
    messages = read_packets(result.stdout)
    responses = {message["id"]: message for message in messages if "id" in message}
    formatted = responses[2]["result"]
    expected = 'hàm chính() {\n    in "chào 🌿";\n}\n;\n'
    if len(formatted) != 1 or formatted[0]["newText"] != expected:
        raise AssertionError(f"LSP native formatter did not preserve UTF-8: {formatted!r}")
    if responses[3].get("error", {}).get("code") != -32603:
        raise AssertionError("LSP did not return a formatting error for invalid source")
    if responses[4].get("result", "missing") is not None:
        raise AssertionError("LSP did not survive a failed formatting request")
    print("Native LSP formatting: unsaved Unicode, error response and shutdown PASS")


if __name__ == "__main__":
    main()
