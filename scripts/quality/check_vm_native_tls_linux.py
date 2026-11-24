#!/usr/bin/env python3
"""Integration-test the Linux ASM/OpenSSL TLS provider without a C harness.

The CA is scoped to the child process via OpenSSL's SSL_CERT_FILE setting.
No certificate is added to the operating system's trust store.
"""

import ctypes
import os
import platform
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ASM = ROOT / "scripts/ASM"

CONTROL_OFFSET = 64
PAYLOAD_OFFSET = 768


def client_mode(library: str, fd: str, hostname: str, timeout: str,
                expect_success: str) -> int:
    """Call the real V++ socket ASM ABI in an isolated subprocess."""
    native = ctypes.CDLL(library)
    native.vpp_native_memory_base.restype = ctypes.c_void_p
    native.vpp_native_os_socket.argtypes = (ctypes.c_int, ctypes.c_int)
    native.vpp_native_os_socket.restype = ctypes.c_int
    base = native.vpp_native_memory_base()
    if not base:
        return 2

    def put_i32(offset: int, value: int) -> None:
        ctypes.memmove(base + CONTROL_OFFSET + offset,
                       struct.pack("=i", value), 4)

    def get_i32(offset: int) -> int:
        return struct.unpack("=i", ctypes.string_at(
            base + CONTROL_OFFSET + offset, 4))[0]

    def call(mode: int) -> int:
        return native.vpp_native_os_socket(mode, CONTROL_OFFSET)

    name = hostname.encode("ascii")
    put_i32(0, int(fd))
    put_i32(12, int(timeout))
    put_i32(16, 512)
    put_i32(20, len(name))
    ctypes.memmove(base + 512, name, len(name))

    upgraded = call(11)
    verified = get_i32(144)
    put_i32(16, PAYLOAD_OFFSET)
    put_i32(20, 5)
    ctypes.memmove(base + PAYLOAD_OFFSET, b"hello", 5)
    written = call(12)
    ctypes.memset(base + PAYLOAD_OFFSET, 0, 5)
    received = 0
    if upgraded == 0:
        # Separate TLS records may arrive in different socket reads.
        while received < 5:
            put_i32(16, PAYLOAD_OFFSET + received)
            put_i32(20, 5 - received)
            part = call(13)
            if part <= 0:
                received = part
                break
            received += part
    else:
        received = call(13)

    valid_payload = (received == 5 and
                     ctypes.string_at(base + PAYLOAD_OFFSET, 5) == b"world")
    released = call(14)
    released_again = call(14)
    closed = call(5)
    read_after_close = call(13)
    print(f"upgrade={upgraded} verified={verified} write={written} "
          f"read={received} world={int(valid_payload)} "
          f"close={released} close_again={released_again}", flush=True)

    if (get_i32(144) != 0 or released != 0 or released_again != 0 or
            closed != 0 or read_after_close != -1):
        return 1
    if int(expect_success):
        return int(not (upgraded == 0 and verified == 1 and
                        written == 5 and valid_payload))
    return int(not (upgraded == -1 and verified == 0 and
                    written == -1 and received == -1))


def run(*arguments: str) -> None:
    result = subprocess.run(arguments, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{' '.join(arguments)}: {result.stderr}")


def certificates(work: Path) -> None:
    extensions = work / "leaf.ext"
    extensions.write_text(
        "basicConstraints=critical,CA:FALSE\n"
        "keyUsage=critical,digitalSignature,keyEncipherment\n"
        "extendedKeyUsage=serverAuth\n"
        "subjectAltName=DNS:localhost\n"
    )
    run("openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
        "-days", "1", "-keyout", str(work / "root.key"),
        "-out", str(work / "root.pem"), "-subj", "/CN=VPP TLS Test Root",
        "-addext", "basicConstraints=critical,CA:TRUE")
    run("openssl", "req", "-newkey", "rsa:2048", "-nodes",
        "-keyout", str(work / "leaf.key"), "-out", str(work / "leaf.csr"),
        "-subj", "/CN=localhost")
    run("openssl", "x509", "-req", "-in", str(work / "leaf.csr"),
        "-CA", str(work / "root.pem"), "-CAkey", str(work / "root.key"),
        "-CAcreateserial", "-out", str(work / "leaf.pem"),
        "-days", "1", "-sha256", "-extfile", str(extensions))


def exercise(client_bin: Path, work: Path, hostname: str, *,
             trusted: bool, silent: bool = False,
             disconnected: bool = False) -> None:
    client, server = socket.socketpair()
    server_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    server_ctx.load_cert_chain(str(work / "leaf.pem"), str(work / "leaf.key"))
    server_errors = []

    def serve() -> None:
        try:
            if disconnected:
                return
            if silent:
                threading.Event().wait(0.5)
                return
            with server_ctx.wrap_socket(server, server_side=True) as connection:
                message = connection.recv(5)
                if message == b"hello":
                    for fragment in (b"w", b"or", b"ld"):
                        connection.sendall(fragment)
        except (OSError, ssl.SSLError) as exc:
            server_errors.append(str(exc))
        finally:
            server.close()

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    env = os.environ.copy()
    if trusted:
        env["SSL_CERT_FILE"] = str(work / "root.pem")
    else:
        env["SSL_CERT_FILE"] = str(work / "nonexistent.pem")
    expect_success = trusted and hostname == "localhost" and not (silent or disconnected)
    label = f"{'trusted' if trusted else 'untrusted'} {hostname}"
    if silent:
        label += " timeout"
    if disconnected:
        label += " disconnect"
    try:
        result = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()), "--client",
             str(client_bin), str(client.fileno()), hostname,
             "100" if silent else "2000", str(int(expect_success))],
            pass_fds=(client.fileno(),), capture_output=True, text=True,
            env=env, timeout=5,
        )
    finally:
        client.close()
        thread.join(timeout=2)
    print(f"{label}: {result.stdout.strip()}", flush=True)
    if result.returncode:
        raise AssertionError(f"{label} failed ({result.returncode}): "
                             f"{result.stderr} {server_errors}")


def main() -> None:
    if len(sys.argv) == 7 and sys.argv[1] == "--client":
        raise SystemExit(client_mode(*sys.argv[2:]))
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "aarch64"):
        raise SystemExit("Linux x86-64 or ARM64 required")
    with tempfile.TemporaryDirectory(prefix="vpp-linux-tls-") as directory:
        work = Path(directory)
        client_bin = work / "libvpp-native-tls-test.so"
        memory = ASM / "linux/vpp_native_tls_test_memory.S"
        if platform.machine() == "x86_64":
            provider = ASM / "linux/vpp_native_tls_linux_x86_64.S"
            boundary = [ASM / "linux/vpp_linux_socket_x86_64.S",
                        ASM / "linux/vpp_linux_syscalls_x86_64.S"]
        else:
            provider = ASM / "linux/vpp_native_tls_linux_aarch64.S"
            socket_asm = work / "socket.S"
            socket_asm.write_text('#include "common/posix/vpp_vm_socket_primitives.inc"\n')
            boundary = [socket_asm]
        run("cc", "-shared", "-fPIC", "-Wl,-z,noexecstack",
            "-DVPP_LINUX_OPENSSL_TLS=1", f"-I{ASM}", str(memory),
            str(provider), *map(str, boundary), "-o", str(client_bin),
            "-lssl", "-lcrypto")
        certificates(work)
        exercise(client_bin, work, "localhost", trusted=True)
        exercise(client_bin, work, "other.local", trusted=True)
        exercise(client_bin, work, "localhost", trusted=False)
        exercise(client_bin, work, "localhost", trusted=True, disconnected=True)
        exercise(client_bin, work, "localhost", trusted=True, silent=True)
    print(f"Linux {platform.machine()} native TLS checks passed")


if __name__ == "__main__":
    main()
