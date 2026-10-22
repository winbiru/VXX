"""Offline localhost DNS failover -> UDP TC=1 -> TCP, in Interpreter/JIT.

The DNS implementation under test is V++; Python only runs a mock server.
The first nameserver is an unused 127/8 loopback address, so its transport
failure must cause retry against the second configured nameserver.
"""

import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading
import time

from check_dns_tcp_fallback import serve


ROOT = Path(__file__).resolve().parents[2]
CLIENT = ROOT / "src/tests/kiem_tra_dns_tcp_fallback.vi"
POLICY_CLIENT = ROOT / "src/tests/kiem_tra_dns_chinh_sach_retry.vi"
PARTIAL_CLIENT = ROOT / "src/tests/kiem_tra_dns_ket_qua_mot_loai.vi"


def check_retry_policy(executable, jit, mode):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        udp.bind(("127.0.0.1", 0))
        port = udp.getsockname()[1]
        udp.settimeout(10)
        errors = []

        def serve_once():
            try:
                query, peer = udp.recvfrom(4096)
                answer = (
                    query[:2] + b"\x81\x80\x00\x01\x00\x00\x00\x00\x00\x00"
                    + query[12:]
                )
                if mode == "bad_id":
                    answer = bytes([answer[0] ^ 1]) + answer[1:]
                elif mode in ("nxdomain", "formerr", "notimp", "unknown"):
                    rcode = {"nxdomain": 3, "formerr": 1, "notimp": 4, "unknown": 15}[mode]
                    answer = answer[:3] + bytes([0x80 | rcode]) + answer[4:]
                udp.sendto(answer, peer)
            except Exception as exc:
                errors.append(exc)

        worker = threading.Thread(target=serve_once, daemon=True)
        worker.start()
        env = os.environ.copy()
        env.update(
            VPP_HOME=str(ROOT), VPP_ENABLE_JIT=jit,
            VPP_DNS_VI_PORT=str(port), VPP_DNS_TEST_MODE=mode,
        )
        started = time.monotonic()
        result = subprocess.run(
            [str(executable), str(POLICY_CLIENT)], cwd=ROOT, env=env,
            capture_output=True, text=True, check=False, timeout=15,
        )
        elapsed = time.monotonic() - started
        worker.join(timeout=2)
        if worker.is_alive() or errors:
            raise AssertionError(f"DNS retry policy mock failed: {errors}")
        if result.returncode or result.stdout != "1\n":
            raise AssertionError(
                f"DNS retry policy {mode} JIT={jit}: exit={result.returncode}, "
                f"stdout={result.stdout!r}, stderr={result.stderr!r}"
            )
        if elapsed > 8:
            raise AssertionError(
                f"DNS retry policy {mode} JIT={jit}: unexpected retry ({elapsed:.1f}s)"
            )


def check_rcode_retry(executable, jit, first_rcode, exhausted=False):
    """SERVFAIL/REFUSED should retry a second nameserver, including over IPv6."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as first, socket.socket(
        socket.AF_INET6, socket.SOCK_DGRAM
    ) as second:
        first.bind(("127.0.0.1", 0))
        port = first.getsockname()[1]
        second.bind(("::1", port))
        first.settimeout(8)
        second.settimeout(8)
        errors = []

        def serve_pair():
            try:
                query, peer = first.recvfrom(4096)
                if struct.unpack("!H", query[-4:-2])[0] != 1:
                    raise AssertionError("expected A query at first server")
                first.sendto(
                    query[:2] + bytes([0x81, 0x80 | first_rcode])
                    + b"\x00\x01\x00\x00\x00\x00\x00\x00" + query[12:], peer,
                )
                query, peer = second.recvfrom(4096)
                if struct.unpack("!H", query[-4:-2])[0] != 1:
                    raise AssertionError("expected A query at second server")
                response = (
                    query[:2] + b"\x81\x80\x00\x01\x00\x01\x00\x00\x00\x00"
                    + query[12:] + b"\xc0\x0c\x00\x01\x00\x01"
                    + b"\x00\x00\x00\x3c\x00\x04\xcb\x00\x71\x2f"
                )
                if exhausted:
                    response = (
                        query[:2] + b"\x81\x85\x00\x01\x00\x00\x00\x00\x00\x00"
                        + query[12:]
                    )
                second.sendto(response, peer)
            except Exception as exc:
                errors.append(exc)

        worker = threading.Thread(target=serve_pair, daemon=True)
        worker.start()
        env = os.environ.copy()
        env.update(
            VPP_HOME=str(ROOT), VPP_ENABLE_JIT=jit,
            VPP_DNS_VI_PORT=str(port),
            VPP_DNS_TEST_MODE="rcode_exhausted" if exhausted else "rcode_retry",
        )
        result = subprocess.run(
            [str(executable), str(POLICY_CLIENT)], cwd=ROOT, env=env,
            capture_output=True, text=True, check=False, timeout=12,
        )
        worker.join(timeout=2)
        if worker.is_alive() or errors:
            raise AssertionError(f"DNS RCODE mock failed: {errors}")
        if result.returncode or result.stdout != "1\n":
            raise AssertionError(
                f"DNS RCODE retry {first_rcode} exhausted={exhausted} JIT={jit}: "
                f"exit={result.returncode}, stdout={result.stdout!r}, "
                f"stderr={result.stderr!r}"
            )


def check_partial_result(executable):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        udp.bind(("127.0.0.1", 0))
        port = udp.getsockname()[1]
        udp.settimeout(25)
        errors = []

        def serve_partial():
            try:
                for expected_type in (1, 28, 1, 28):
                    query, peer = udp.recvfrom(4096)
                    qtype = struct.unpack("!H", query[-4:-2])[0]
                    if qtype != expected_type:
                        raise AssertionError(f"expected QTYPE {expected_type}, got {qtype}")
                    if qtype == 1:
                        answer = (
                            query[:2] + b"\x81\x80\x00\x01\x00\x01\x00\x00\x00\x00"
                            + query[12:] + b"\xc0\x0c\x00\x01\x00\x01"
                            + b"\x00\x00\x00\x3c\x00\x04\xc6\x33\x64\x17"
                        )
                        udp.sendto(answer, peer)
                    # AAAA intentionally times out.
            except Exception as exc:
                errors.append(exc)

        worker = threading.Thread(target=serve_partial, daemon=True)
        worker.start()
        for jit in ("0", "1"):
            env = os.environ.copy()
            env.update(
                VPP_HOME=str(ROOT), VPP_ENABLE_JIT=jit,
                VPP_DNS_VI_SERVERS="127.0.0.1",
                VPP_DNS_VI_SERVER="", VPP_DNS_VI_RESOLV_CONF="",
                VPP_DNS_VI_PORT=str(port),
            )
            result = subprocess.run(
                [str(executable), str(PARTIAL_CLIENT)], cwd=ROOT, env=env,
                capture_output=True, text=True, check=False, timeout=15,
            )
            if result.returncode or result.stdout != "1\n":
                raise AssertionError(
                    f"DNS partial result JIT={jit}: exit={result.returncode}, "
                    f"stdout={result.stdout!r}, stderr={result.stderr!r}"
                )
        worker.join(timeout=2)
        if worker.is_alive() or errors:
            raise AssertionError(f"DNS partial-result mock failed: {errors}")


def main():
    executable = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/bin/vpp-cli").resolve()
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp, socket.socket(
        socket.AF_INET, socket.SOCK_DGRAM
    ) as udp:
        tcp.bind(("127.0.0.1", 0))
        port = tcp.getsockname()[1]
        tcp.listen(4)
        tcp.settimeout(35)
        udp.bind(("127.0.0.1", port))
        udp.settimeout(35)
        errors = []
        worker = threading.Thread(target=serve, args=(udp, tcp, errors), daemon=True)
        worker.start()
        for jit in ("0", "1"):
            env = os.environ.copy()
            env.update(
                VPP_HOME=str(ROOT),
                VPP_ENABLE_JIT=jit,
                VPP_DNS_VI_SERVERS="127.0.0.2, 127.0.0.1",
                VPP_DNS_VI_SERVER="",
                VPP_DNS_VI_RESOLV_CONF="",
                VPP_DNS_VI_PORT=str(port),
            )
            result = subprocess.run(
                [str(executable), str(CLIENT)], cwd=ROOT, env=env,
                capture_output=True, text=True, check=False, timeout=35,
            )
            if result.returncode or result.stdout != "1\n1\n1\n":
                raise AssertionError(
                    f"DNS failover JIT={jit}: exit={result.returncode}, "
                    f"stdout={result.stdout!r}, stderr={result.stderr!r}"
                )
        worker.join(timeout=4)
        if worker.is_alive():
            raise TimeoutError("DNS multi-server mock did not receive four queries")
        if errors:
            raise errors[0]
    for jit in ("0", "1"):
        for mode in ("bad_id", "nodata", "nxdomain", "formerr", "notimp", "unknown"):
            check_retry_policy(executable, jit, mode)
        check_rcode_retry(executable, jit, 2)
        check_rcode_retry(executable, jit, 5)
        check_rcode_retry(executable, jit, 2, exhausted=True)
    check_partial_result(executable)
    print("DNS failover, TCP, RCODE retry policy and partial A/AAAA results, Interpreter/JIT: PASS")


if __name__ == "__main__":
    main()
