"""Loopback-only integration test of DNS UDP TC=1 -> TCP in V++.

Requires a sandbox permitting localhost bind/connect. The Python server is
test infrastructure; DNS encoding, framing, parsing and socket logic under
test all execute in .vi through Machine Socket ABI.
"""

import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading
import time


ROOT = Path(__file__).resolve().parents[2]
CLIENT = ROOT / "src/tests/kiem_tra_dns_tcp_fallback.vi"
NEGATIVE_CLIENT = ROOT / "src/tests/kiem_tra_dns_tcp_negative.vi"

# Invalid UDP responses must be rejected before attempting TCP. Other cases
# must reach TCP and reject a malformed frame or DNS message there.
NEGATIVE_CASES = (
    "udp_bad_id", "udp_bad_qname", "udp_bad_qtype", "udp_bad_qclass",
    "udp_bad_z", "udp_bad_qdcount", "udp_short_question",
    "tcp_short_prefix", "tcp_short_body", "tcp_short_length",
    "tcp_bad_id", "tcp_bad_qname", "tcp_bad_qtype", "tcp_bad_z",
    "tcp_slow_trickle",
)


def receive_exact(connection, count):
    result = bytearray()
    while len(result) < count:
        part = connection.recv(count - len(result))
        if not part:
            raise RuntimeError("DNS TCP test client disconnected prematurely")
        result.extend(part)
    return bytes(result)


def serve(udp, tcp, errors):
    try:
        for _ in range(4):  # A/AAAA, on Interpreter and JIT.
            query, peer = udp.recvfrom(4096)
            if len(query) < 17:
                raise ValueError("short DNS UDP query")
            udp.sendto(query[:2] + b"\x83\x80\x00\x01\x00\x00\x00\x00\x00\x00" + query[12:], peer)

            connection, _ = tcp.accept()
            with connection:
                connection.settimeout(20)
                count = struct.unpack("!H", receive_exact(connection, 2))[0]
                payload = receive_exact(connection, count)
                qtype = struct.unpack("!H", payload[-4:-2])[0]
                if qtype == 1:
                    address = bytes([198, 51, 100, 42])
                elif qtype == 28:
                    address = bytes.fromhex("20010db8000000000000000000000005")
                else:
                    raise ValueError(f"unexpected DNS question type {qtype}")
                response = (
                    payload[:2]
                    + b"\x81\x80\x00\x01\x00\x01\x00\x00\x00\x00"
                    + payload[12:]
                    + b"\xc0\x0c"
                    + payload[-4:]
                    + b"\x00\x00\x00\x3c"
                    + struct.pack("!H", len(address))
                    + address
                )
                frame = struct.pack("!H", len(response)) + response
                # Force partial reads of the two-byte length and response body.
                connection.sendall(frame[:1])
                threading.Event().wait(0.02)
                connection.sendall(frame[1:2])
                threading.Event().wait(0.02)
                for start in range(2, len(frame), 7):
                    connection.sendall(frame[start:start + 7])
    except Exception as exc:
        errors.append(exc)


def alter_question(response, mode):
    """Mutate a valid example.com / A packet while preserving its framing."""
    packet = bytearray(response)
    if mode.endswith("bad_id"):
        packet[0] ^= 1
    elif mode.endswith("bad_qname"):
        packet[13] = ord("x")
    elif mode.endswith("bad_qtype"):
        # Both response layouts contain QTYPE immediately after QNAME.
        packet[25:27] = b"\x00\x1c"
    elif mode.endswith("bad_qclass"):
        packet[27:29] = b"\x00\x02"
    elif mode.endswith("bad_z"):
        packet[3] |= 0x40
    elif mode == "udp_bad_qdcount":
        packet[5] = 0
    elif mode == "udp_short_question":
        return bytes(packet[:18])
    return bytes(packet)


def serve_negative(udp, tcp, mode, errors, query_started):
    try:
        query, peer = udp.recvfrom(4096)
        query_started.append(time.monotonic())
        if len(query) < 29 or query[-4:] != b"\x00\x01\x00\x01":
            raise ValueError(f"unexpected negative fixture DNS question: {query!r}")
        truncated = query[:2] + b"\x83\x80\x00\x01\x00\x00\x00\x00\x00\x00" + query[12:]
        if mode.startswith("udp_"):
            udp.sendto(alter_question(truncated, mode), peer)
            # A broken client may still connect to TCP after the invalid UDP
            # reply. Treat that as a failure even if the .vi catches its error.
            tcp.settimeout(0.6)
            try:
                conn, _ = tcp.accept()
            except socket.timeout:
                return
            with conn:
                raise AssertionError(f"{mode}: unexpected TCP fallback")

        udp.sendto(truncated, peer)
        connection, _ = tcp.accept()
        with connection:
            connection.settimeout(10)
            count = struct.unpack("!H", receive_exact(connection, 2))[0]
            received = receive_exact(connection, count)
            if received != query:
                raise AssertionError(f"{mode}: TCP query differs from UDP query")
            if mode == "tcp_short_prefix":
                connection.sendall(b"\x00")
                return
            if mode == "tcp_short_length":
                connection.sendall(b"\x00\x0b")
                return
            response = (
                query[:2] + b"\x81\x80\x00\x01\x00\x01\x00\x00\x00\x00"
                + query[12:] + b"\xc0\x0c\x00\x01\x00\x01"
                + b"\x00\x00\x00\x3c\x00\x04\xc6\x33\x64\x2a"
            )
            response = alter_question(response, mode)
            frame = struct.pack("!H", len(response)) + response
            if mode == "tcp_short_body":
                connection.sendall(frame[:7])
            elif mode == "tcp_slow_trickle":
                connection.sendall(frame[:3])
                for _ in range(16):
                    threading.Event().wait(0.3)
                    try:
                        connection.sendall(b"\x00")
                    except (BrokenPipeError, ConnectionResetError):
                        # Expected: V++ closes the socket on global deadline.
                        return
            else:
                connection.sendall(frame)
    except Exception as exc:
        errors.append(exc)


def check_negative(executable, jit, mode):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp, socket.socket(
        socket.AF_INET, socket.SOCK_DGRAM
    ) as udp:
        tcp.bind(("127.0.0.1", 0))
        port = tcp.getsockname()[1]
        tcp.listen(1)
        tcp.settimeout(10)
        udp.bind(("127.0.0.1", port))
        udp.settimeout(10)
        errors = []
        query_started = []
        worker = threading.Thread(
            target=serve_negative, args=(udp, tcp, mode, errors, query_started), daemon=True
        )
        worker.start()
        environment = os.environ.copy()
        environment.update(
            VPP_HOME=str(ROOT), VPP_ENABLE_JIT=jit,
            VPP_DNS_VI_SERVER="127.0.0.1", VPP_DNS_VI_PORT=str(port),
        )
        result = subprocess.run(
            [str(executable), str(NEGATIVE_CLIENT)], cwd=ROOT, env=environment,
            capture_output=True, text=True, timeout=15, check=False,
        )
        elapsed = time.monotonic() - query_started[0] if query_started else 0
        worker.join(timeout=11)
        if worker.is_alive():
            raise TimeoutError(f"{mode} JIT={jit}: mock server stalled")
        if errors:
            raise AssertionError(f"{mode} JIT={jit}: {errors[0]}") from errors[0]
        if result.returncode or result.stdout != "1\n":
            raise AssertionError(
                f"{mode} JIT={jit}: expected DNS rejection, got "
                f"exit={result.returncode}, stdout={result.stdout!r}, "
                f"stderr={result.stderr!r}"
            )
        # Deadline được tính từ khi server nhận query; thời gian khởi động
        # CLI/interpreter không thuộc giao dịch DNS.
        if mode == "tcp_slow_trickle" and elapsed > 3.2:
            raise AssertionError(
                f"{mode} JIT={jit}: DNS transaction deadline exceeded ({elapsed:.2f}s)"
            )


def main():
    executable = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/bin/vpp-cli").resolve()
    if not executable.is_file():
        raise FileNotFoundError(executable)
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp, socket.socket(
        socket.AF_INET, socket.SOCK_DGRAM
    ) as udp:
        tcp.bind(("127.0.0.1", 0))
        port = tcp.getsockname()[1]
        tcp.listen(1)
        tcp.settimeout(30)
        udp.bind(("127.0.0.1", port))
        udp.settimeout(30)
        errors = []
        worker = threading.Thread(target=serve, args=(udp, tcp, errors), daemon=True)
        worker.start()
        for jit in ("0", "1"):
            environment = os.environ.copy()
            environment.update(
                VPP_HOME=str(ROOT),
                VPP_ENABLE_JIT=jit,
                VPP_DNS_VI_SERVER="127.0.0.1",
                VPP_DNS_VI_PORT=str(port),
            )
            result = subprocess.run(
                [str(executable), str(CLIENT)],
                cwd=ROOT,
                env=environment,
                capture_output=True,
                text=True,
                timeout=40,
                check=False,
            )
            if result.returncode or result.stdout != "1\n1\n1\n":
                raise AssertionError(
                    f"DNS TCP fallback JIT={jit} failed ({result.returncode}): "
                    f"{result.stdout!r}\n{result.stderr}"
                )
        worker.join(timeout=5)
        if worker.is_alive():
            raise TimeoutError("DNS TCP mock did not finish handling queries")
        if errors:
            raise errors[0]
    for jit in ("0", "1"):
        for mode in NEGATIVE_CASES:
            check_negative(executable, jit, mode)
    print(f"DNS TCP fallback and {len(NEGATIVE_CASES)} negative cases, Interpreter/JIT: PASS")


if __name__ == "__main__":
    main()
