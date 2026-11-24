#!/usr/bin/env python3
"""Verify pure V++ TLS fixtures, certificate paths and live TLS 1.3 interoperability."""

import argparse
import contextlib
import hashlib
import hmac
import json
import os
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

from check_vm_tls_vpp_cert_paths import ROOT, authority, leaf
from run_native_regression import execute

RESPONSE = b"TLS VPP INTEROP RESPONSE"
SUCCESS = "TLS VPP REAL OPENSSL SERVER INTEROP OK"


def fixture(port, root_pem):
    return f"""nhập gói.bảo mật.kết nối TLS.Kết Nối TLS VPP;
nhập gói.bảo mật.chứng chỉ TLS.Chứng Chỉ TLS VPP;

hàm tls kiểm tra đóng(client) {{
    rejected = 0;
    thử {{ client.nhận(); }} bắt lỗi(e) {{
        nếu (e != "TLS: connection not authenticated") {{ ném e; }};
        rejected = rejected + 1;
    }};
    thử {{ client.gửi([42]); }} bắt lỗi(e) {{
        nếu (e != "TLS: connection not authenticated") {{ ném e; }};
        rejected = rejected + 1;
    }};
    nếu (rejected != 2) {{ ném "TLS: closed connection accepted I/O"; }};
    client.đóng();
    client.đóng();
}};

hàm chính() {{
    decoder = Chứng Chỉ TLS VPP();
    anchor = decoder.đọc pem({json.dumps(root_pem)});
    request = chuỗi thành byte máy ảo("GET / HTTP/1.0\\r\\nHost: localhost\\r\\n\\r\\n");

    client = Kết Nối TLS VPP();
    client.mở("localhost", {port}, [anchor], 30000);
    client.gửi(request);
    response = chuỗi từ byte máy ảo(client.nhận());
    nếu (response != "{RESPONSE.decode()}") {{ ném "TLS: application data mismatch"; }};
    nếu (độ dài(client.nhận()) != 0) {{ ném "TLS: expected close_notify"; }};
    tls kiểm tra đóng(client);
    in "TLS VPP FRAGMENTED TICKETS AND CLOSE_NOTIFY OK";

    client = Kết Nối TLS VPP();
    client.mở("localhost", {port}, [anchor], 30000);
    client.gửi(request);
    rejected = sai;
    thử {{ client.nhận(); }} bắt lỗi(e) {{
        nếu (e != "TLS: interrupted post-handshake message") {{ ném e; }};
        rejected = đúng;
    }};
    nếu (!rejected) {{ ném "TLS: interleaved application data accepted"; }};
    thử {{ client.mở("localhost", {port}, [anchor]); }} bắt lỗi(e) {{
        nếu (e != "TLS: connection already used") {{ ném e; }};
    }};
    tls kiểm tra đóng(client);
    in "{SUCCESS}";
}};
"""


def ticket(nonce):
    body = bytes.fromhex("0000003c 00000001 01") + bytes([nonce]) + bytes.fromhex("0001 61 0000")
    return b"\x04" + len(body).to_bytes(3, "big") + body


def encrypted_records(keylog, records):
    from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305

    secret = next(
        bytes.fromhex(line.split()[2])
        for line in reversed(keylog.read_text().splitlines())
        if line.startswith("SERVER_TRAFFIC_SECRET_0 ")
    )

    def expand(label, size):
        # RFC 8446 HKDF-Expand-Label with an empty context and one SHA-256 block.
        info = size.to_bytes(2, "big") + bytes([6 + len(label)]) + b"tls13 " + label + b"\0"
        return hmac.digest(secret, info + b"\x01", hashlib.sha256)[:size]

    cipher = ChaCha20Poly1305(expand(b"key", 32))
    iv = int.from_bytes(expand(b"iv", 12), "big")
    for sequence, (kind, payload) in enumerate(records):
        plaintext = payload + bytes([kind])
        header = b"\x17\x03\x03" + (len(plaintext) + 16).to_bytes(2, "big")
        nonce = (iv ^ sequence).to_bytes(12, "big")
        yield header + cipher.encrypt(nonce, plaintext, header)


def serve(server, context, keylog):
    first, second = ticket(1), ticket(2)
    fragmented = [
        (22, first[:2]),             # Split the handshake header.
        (22, first[2:7]),            # Split the body.
        (22, first[7:] + second),    # Coalesce the remaining body and another ticket.
        (23, b""),                   # An empty application record is legal.
        (23, RESPONSE),
        (21, b"\x01\x00"),          # close_notify, the only clean EOF.
    ]
    interrupted = [(22, first[:7]), (23, RESPONSE)]
    for records, clean_close in ((fragmented, True), (interrupted, False)):
        sock, _ = server.accept()
        with sock:
            sock.settimeout(420)
            with context.wrap_socket(sock, server_side=True, suppress_ragged_eofs=False) as tls:
                if tls.cipher()[0] != "TLS_CHACHA20_POLY1305_SHA256":
                    raise AssertionError(f"unexpected TLS cipher {tls.cipher()}")
                request = b""
                while not request.endswith(b"\r\n\r\n"):
                    chunk = tls.recv(4096)
                    if not chunk or len(request) + len(chunk) > 4096:
                        raise AssertionError("incomplete or oversized V++ client request")
                    request += chunk
                if not request.startswith(b"GET / HTTP/1.0\r\n"):
                    raise AssertionError("unexpected V++ client request")
                # OpenSSL performed the real handshake. Emit authenticated application
                # records on its TCP stream with deterministic TLS record boundaries.
                socket.socket.sendall(tls, b"".join(encrypted_records(keylog, records)))
                if clean_close and tls.recv(1) != b"":
                    raise AssertionError("expected the client's authenticated close_notify")
                if socket.socket.recv(tls, 1) != b"":
                    raise AssertionError("V++ client did not close the TCP stream")


def check_interop():
    with tempfile.TemporaryDirectory(prefix="vpp_tls_interop_") as tmp:
        directory = Path(tmp)
        root = authority(directory, "VPP-local-root")
        cert = leaf(directory, "localhost", root)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_3
        context.maximum_version = ssl.TLSVersion.TLSv1_3
        context.num_tickets = 0  # Keep the server's application record sequence at zero.
        context.load_cert_chain(cert, directory / "localhost.key")
        keylog = directory / "keylog"
        context.keylog_filename = str(keylog)
        hosts = directory / "hosts"
        hosts.write_text("127.0.0.1 localhost\n", encoding="ascii")
        env = dict(os.environ, VPP_DNS_VI_HOSTS_FILE=hosts.as_posix())
        errors = []

        with contextlib.ExitStack() as cleanup, socket.socket() as server:
            # Windows keeps the keylog file locked while SSLContext holds it.
            cleanup.callback(setattr, context, "keylog_filename", None)
            server.bind(("127.0.0.1", 0))
            server.listen(1)
            server.settimeout(420)

            def run_server():
                try:
                    serve(server, context, keylog)
                except Exception as exc:
                    errors.append(exc)

            with tempfile.NamedTemporaryFile(
                prefix=".vm_tls_live_", suffix=".vi", dir=ROOT / "scripts/test/fixtures", mode="w",
                encoding="utf-8", delete=False,
            ) as source:
                source.write(fixture(server.getsockname()[1], root[1].read_text()))
                source_path = Path(source.name)
            program = directory / ("client.exe" if os.name == "nt" else "client")
            start = time.monotonic()
            try:
                subprocess.run(
                    [os.environ.get("VPP_TLS_TEST_BINARY", str(ROOT / "VPP")),
                     "build", str(source_path.relative_to(ROOT)), "-o", str(program)],
                    cwd=ROOT, env=env, check=True, timeout=300,
                )
            finally:
                source_path.unlink()
            worker = threading.Thread(target=run_server, daemon=True)
            worker.start()
            result = subprocess.run(
                [str(program)], cwd=ROOT, env=env, capture_output=True, text=True,
                timeout=1200 - (time.monotonic() - start),
            )

            print(result.stdout, end="")
            print(result.stderr, end="")
            if result.returncode:
                if errors:
                    print(f"Local TLS server error: {errors[0]!r}")
                raise SystemExit(result.returncode)
            if SUCCESS not in result.stdout.splitlines():
                raise AssertionError("V++ TLS interoperability fixture did not finish")
            worker.join(timeout=5)
            if errors:
                raise RuntimeError("local TLS server failed") from errors[0]
            if worker.is_alive():
                raise RuntimeError("local TLS server did not finish")


FIXTURES = {
    "authenticated_handshake": "TLS VPP AUTHENTICATED FLIGHT OK",
    "ed25519": "Ed25519 VPP RFC VECTOR OK",
    "handshake": "TLS VPP HANDSHAKE FRAMING OK",
    "x25519": "X25519 VPP RFC VECTORS OK",
    "certificates": "TLS VPP CERTIFICATE PARSING OK",
    "client": "TLS VPP CLIENT API OK",
    "sha512": "SHA512 VPP FIPS VECTORS OK",
    "stream": "TLS VPP STREAM REASSEMBLY OK",
}
FIXTURE_TIMEOUT = 900
SCRIPTS = {
    "x509": ("check_vm_tls_vpp_cert_paths.py", 1530),
    "interop": ("check_vm_tls_vpp_interop.py", 1200),
}


def checked(command, env, logs, phase, timeout, expected=None):
    if timeout <= 0:
        raise TimeoutError(f"{logs.name}: fixture time budget exhausted before {phase}")
    try:
        status = execute(command, ROOT, env, logs, phase, timeout)
    except subprocess.TimeoutExpired as error:
        raise TimeoutError(f"{logs.name}/{phase}: timeout after {timeout:.2f}s; see {logs}") from error
    stdout = (logs / (phase + ".stdout")).read_bytes().replace(b"\r\n", b"\n")
    if status or (expected is not None and stdout != (expected + "\n").encode()):
        stderr = (logs / (phase + ".stderr")).read_bytes()
        raise RuntimeError(f"{logs.name}/{phase}: exit {status}\n"
                           f"{stdout[-4000:].decode('utf-8', 'replace')}\n"
                           f"{stderr[-4000:].decode('utf-8', 'replace')}")


def run_fixture(compiler, env, logs, name):
    directory = logs / name
    directory.mkdir(parents=True, exist_ok=True)
    source = ROOT / "scripts/test/fixtures" / f"vm_tls_{name}.vi"
    start = time.monotonic()
    with tempfile.TemporaryDirectory(prefix=f"vpp-tls-{name}-") as temporary:
        binary = Path(temporary) / ("program.exe" if os.name == "nt" else "program")
        checked([compiler, "build", source, "-o", binary], env, directory, "build", FIXTURE_TIMEOUT)
        checked([binary], env, directory, "run", FIXTURE_TIMEOUT - (time.monotonic() - start),
                FIXTURES[name])
    return f"vpp-tls-{name}: PASS ({time.monotonic() - start:.2f}s)"


def main():
    # Preserve the original standalone interoperability entry point.
    if len(sys.argv) == 1:
        check_interop()
        return
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list-fixtures", action="store_true")
    parser.add_argument("--case", choices=(*FIXTURES, *SCRIPTS))
    parser.add_argument("--compiler", type=Path)
    parser.add_argument("--clang", type=Path)
    parser.add_argument("--logs", type=Path)
    args = parser.parse_args()
    if args.list_fixtures:
        print(";".join(FIXTURES))
        return
    if not all((args.case, args.compiler, args.logs)):
        parser.error("--case, --compiler and --logs are required")
    compiler = args.compiler.resolve(strict=True)
    logs = args.logs.resolve()
    env = dict(os.environ)
    if os.name == "nt":
        if args.clang is None:
            parser.error("--clang is required on Windows")
        from check_windows_full_bootstrap import package_env
        env = package_env(compiler.parent.parent, args.clang.resolve(strict=True))
    elif sys.platform.startswith("linux"):
        env["VPP_AOT_LINUX_OBJECT_DIR"] = str(compiler.parent.parent / "share/vpp/native-vpp-runtime/linux-objects")
        # Exercise the V++ TLS implementation using raw sockets, without libssl.
        env["VPP_AOT_LINUX_TLS"] = "0"
    env["VPP_TLS_TEST_BINARY"] = str(compiler)
    try:
        if args.case in FIXTURES:
            print(run_fixture(compiler, env, logs, args.case), flush=True)
        else:
            script, timeout = SCRIPTS[args.case]
            directory = logs / args.case
            directory.mkdir(parents=True, exist_ok=True)
            checked([sys.executable, ROOT / "scripts/quality" / script], env, directory, "run", timeout)
            print((directory / "run.stdout").read_text(encoding="utf-8"), end="", flush=True)
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(error, file=sys.stderr, flush=True)
        raise SystemExit(1) from error



if __name__ == "__main__":
    main()
