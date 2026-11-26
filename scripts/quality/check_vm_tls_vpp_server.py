#!/usr/bin/env python3
"""Live OpenSSL client -> V++ TLS listener/reverse proxy -> HTTP backend."""
import json
import os
import socket
import ssl
import subprocess
import tempfile
import threading
import time
from pathlib import Path

from cryptography.hazmat.primitives import serialization

from check_vm_tls_vpp_cert_paths import ROOT, authority, leaf

BODY = b"hello\x00world\xff"
RESPONSE = b"HTTP/1.1 200 OK\r\nContent-Length: 12\r\nConnection: close\r\n\r\n" + BODY


def reserve_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def source(cert, seed, port, backend):
    return f"""nhập gói.bảo mật.chứng chỉ TLS.Chứng Chỉ TLS VPP;
nhập gói.mạng.http.proxy TLS.Proxy TLS VPP;

hàm chính() {{
    decoder = Chứng Chỉ TLS VPP();
    chain = [decoder.đọc pem({json.dumps(cert)})];
    proxy = Proxy TLS VPP(chain, {list(seed)}, "http://127.0.0.1:{backend}", 120000);
    proxy.mở({port});
    rejected = sai;
    thử {{ proxy.chuyển tiếp(); }} bắt lỗi(e) {{ rejected = đúng; }};
    nếu (!rejected) {{ ném "TLS: malformed client accepted"; }};
    proxy.chuyển tiếp();
    proxy.đóng();
    proxy.đóng();
    in "TLS VPP SERVER AND PROXY OK";
}};
"""


def main():
    with tempfile.TemporaryDirectory(prefix="vpp-tls-server-") as temp:
        directory = Path(temp)
        ca = authority(directory, "root")
        certificate = leaf(directory, "localhost", ca)
        key = serialization.load_pem_private_key((directory / "localhost.key").read_bytes(), None)
        seed = key.private_bytes(serialization.Encoding.Raw, serialization.PrivateFormat.Raw,
                                 serialization.NoEncryption())
        port = reserve_port()
        with socket.socket() as backend:
            backend.bind(("127.0.0.1", 0))
            backend.listen(1)
            backend.settimeout(300)
            errors = []

            def serve():
                try:
                    conn, _ = backend.accept()
                    with conn:
                        conn.settimeout(120)
                        request = b""
                        while b"\r\n\r\n" not in request:
                            part = conn.recv(4096)
                            if not part:
                                raise AssertionError("truncated request")
                            request += part
                        header, body = request.split(b"\r\n\r\n", 1)
                        while len(body) < len(BODY):
                            part = conn.recv(4096)
                            if not part:
                                raise AssertionError("truncated body")
                            body += part
                        assert header.startswith(b"POST /echo?x=1 HTTP/1.1\r\n")
                        assert b"Host: 127.0.0.1:" in header
                        assert b"Connection: close" in header
                        assert body == BODY, body
                        conn.sendall(RESPONSE)
                except Exception as exc:
                    errors.append(exc)

            with tempfile.NamedTemporaryFile(prefix=".vm_tls_server_", suffix=".vi",
                    dir=ROOT / "scripts/test/fixtures", mode="w", encoding="utf-8", delete=False) as f:
                f.write(source(certificate.read_text(), seed, port, backend.getsockname()[1]))
                path = Path(f.name)
            binary = directory / ("server.exe" if os.name == "nt" else "server")
            compiler = os.environ.get("VPP_TLS_TEST_BINARY", str(ROOT / "VPP"))
            try:
                subprocess.run([compiler, "build", str(path), "-o", str(binary)],
                               cwd=ROOT, check=True, timeout=600)
                worker = threading.Thread(target=serve, daemon=True)
                worker.start()
                proc = subprocess.Popen([str(binary)], cwd=ROOT, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE)
                try:
                    deadline = time.monotonic() + 180
                    while True:
                        if proc.poll() is not None:
                            raise RuntimeError(f"server exited: {proc.communicate()}")
                        try:
                            with socket.create_connection(("127.0.0.1", port), timeout=1) as bad:
                                bad.sendall(b"\x16\x03\x03\x00\x01\xff")
                            break
                        except OSError:
                            if time.monotonic() >= deadline:
                                raise
                            time.sleep(0.1)
                    context = ssl.create_default_context(cafile=str(ca[1]))
                    context.minimum_version = ssl.TLSVersion.TLSv1_3
                    context.maximum_version = ssl.TLSVersion.TLSv1_3
                    context.set_ecdh_curve("X25519")
                    with socket.create_connection(("127.0.0.1", port), timeout=180) as raw:
                        with context.wrap_socket(raw, server_hostname="localhost",
                                                 suppress_ragged_eofs=False) as tls:
                            assert tls.cipher()[0] == "TLS_CHACHA20_POLY1305_SHA256"
                            tls.sendall(b"POST /echo?x=1 HTTP/1.1\r\nHost: localhost\r\n"
                                        b"Content-Length: 12\r\nConnection: keep-alive\r\n\r\n")
                            tls.sendall(BODY[:4])
                            tls.sendall(BODY[4:])
                            response = b""
                            while True:
                                part = tls.recv(4096)
                                if not part:
                                    break
                                response += part
                            assert response == RESPONSE, response
                    out, err = proc.communicate(timeout=30)
                    assert proc.returncode == 0, (out, err)
                    assert b"TLS VPP SERVER AND PROXY OK" in out, out
                    worker.join(5)
                    assert not worker.is_alive()
                    if errors:
                        raise errors[0]
                    print(out.decode(), end="")
                finally:
                    if proc.poll() is None:
                        proc.kill()
                        proc.communicate()
            finally:
                path.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
