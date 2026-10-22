"""Mock-only DNS tests of the V++ structured UDP/TCP transport API."""

import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading

from check_dns_tcp_fallback import receive_exact


ROOT = Path(__file__).resolve().parents[2]
CLIENT = ROOT / "src/tests/kiem_tra_dns_transport_mang.vi"
QUERIES = {"a": 3, "aaaa": 1, "zero": 1, "max": 1,
           "nodata": 2, "nxdomain": 2, "tcp": 2, "failover": 1,
           "cache_hit": 1, "cache_zero": 2, "cache_separate": 2,
           "resolve_both": 2, "resolve_partial": 2, "resolve_negative": 2,
           "resolve_mixed": 2, "resolve_incomplete": 2, "resolve_aaaa_only": 2}
RESOLVER_CLIENT = ROOT / "src/tests/kiem_tra_dns_resolver_day_du.vi"


def response(query, mode):
    if len(query) < 29 or query[-2:] != b"\x00\x01":
        raise AssertionError("invalid DNS question")
    qtype = struct.unpack("!H", query[-4:-2])[0]
    if mode == "aaaa":
        if qtype != 28:
            raise AssertionError("expected AAAA question")
        address = bytes.fromhex("20010db8000000000000000000000001")
    else:
        if qtype != 1:
            raise AssertionError("expected A question")
        address = bytes([203, 0, 113, 47])
    rcode = 3 if mode == "nxdomain" else 0
    count = 0 if mode in ("nodata", "nxdomain") else 1
    header = (query[:2] + bytes([0x81, 0x80 | rcode])
              + struct.pack("!HHHH", 1, count, 0, 0))
    result = header + query[12:]
    if count:
        ttl = {"zero": 0, "cache_zero": 0, "max": 4294967295,
               "failover": 41}.get(mode, 60)
        result += (b"\xc0\x0c" + query[-4:] + struct.pack("!IH", ttl, len(address))
                   + address)
    return result


def resolver_response(query, mode):
    qtype = struct.unpack("!H", query[-4:-2])[0]
    if qtype not in (1, 28):
        raise AssertionError(f"unexpected QTYPE: {qtype}")
    rcode = 3 if mode == "resolve_negative" or (mode == "resolve_mixed" and qtype == 1) else 0
    have_address = rcode == 0 and (
        mode == "resolve_both" or (mode == "resolve_partial" and qtype == 1)
        or (mode == "resolve_aaaa_only" and qtype == 28)
    )
    address = bytes([203, 0, 113, 47]) if qtype == 1 else bytes.fromhex(
        "20010db8000000000000000000000001"
    )
    result = (query[:2] + bytes([0x81, 0x80 | rcode])
              + struct.pack("!HHHH", 1, int(have_address), 0, 0) + query[12:])
    if have_address:
        result += (b"\xc0\x0c" + query[-4:]
                   + struct.pack("!IH", 60 if qtype == 1 else 25, len(address)) + address)
    return result


def run_case(executable, jit, mode):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        udp.bind(("127.0.0.1", 0))
        port = udp.getsockname()[1]
        udp.settimeout(9)
        failures = []
        tcp = None
        second = None
        if mode == "tcp":
            tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            tcp.bind(("127.0.0.1", port))
            tcp.listen(2)
            tcp.settimeout(9)
        if mode == "failover":
            second = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
            second.bind(("::1", port))
            second.settimeout(9)

        def serve():
            try:
                for _ in range(QUERIES[mode]):
                    query, peer = udp.recvfrom(4096)
                    if mode.startswith("resolve_"):
                        qtype = struct.unpack("!H", query[-4:-2])[0]
                        if mode in ("resolve_partial", "resolve_incomplete") and qtype == 28:
                            continue  # AAAA timeout must not erase a valid A result.
                        udp.sendto(resolver_response(query, mode), peer)
                    elif mode == "tcp":
                        truncated = (query[:2] + b"\x83\x80\x00\x01"
                                     + b"\x00\x00\x00\x00\x00\x00" + query[12:])
                        udp.sendto(truncated, peer)
                        conn, _ = tcp.accept()
                        with conn:
                            size = struct.unpack("!H", receive_exact(conn, 2))[0]
                            forwarded = receive_exact(conn, size)
                            if forwarded != query:
                                raise AssertionError("TCP query differs from UDP query")
                            payload = response(query, mode)
                            frame = struct.pack("!H", len(payload)) + payload
                            conn.sendall(frame[:1])
                            conn.sendall(frame[1:5])
                            conn.sendall(frame[5:])
                    elif mode == "failover":
                        servfail = (query[:2] + b"\x81\x82\x00\x01"
                                    + b"\x00\x00\x00\x00\x00\x00" + query[12:])
                        udp.sendto(servfail, peer)
                        follow_up, peer6 = second.recvfrom(4096)
                        second.sendto(response(follow_up, mode), peer6)
                    else:
                        udp.sendto(response(query, mode), peer)
            except Exception as exc:
                failures.append(exc)

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        try:
            env = os.environ.copy()
            env.update(VPP_HOME=str(ROOT), VPP_ENABLE_JIT=str(jit),
                       VPP_DNS_TEST_MODE=mode, VPP_DNS_VI_PORT=str(port))
            if mode.startswith("resolve_"):
                env.update(VPP_DNS_VI_SERVERS="127.0.0.1", VPP_DNS_VI_SERVER="",
                           VPP_DNS_VI_RESOLV_CONF="", VPP_DNS_VI_HOSTS_FILE="")
            result = subprocess.run(
                [str(executable), str(RESOLVER_CLIENT if mode.startswith("resolve_") else CLIENT)],
                cwd=ROOT, env=env,
                capture_output=True, text=True, check=False, timeout=18,
            )
            worker.join(timeout=10)
            if worker.is_alive() or failures:
                raise AssertionError(
                    f"mode={mode} JIT={jit}, mock timed out/failed: {failures}, "
                    f"client rc={result.returncode}, stdout={result.stdout!r}, "
                    f"stderr={result.stderr!r}"
                )
            expected = "1\n" * (4 if mode == "a" else 2 if mode in
                                     ("nodata", "nxdomain", "tcp") else 1)
            if result.returncode != 0 or result.stdout != expected:
                raise AssertionError(
                    f"mode={mode} JIT={jit}, rc={result.returncode}, "
                    f"stdout={result.stdout!r}, stderr={result.stderr!r}"
                )
        finally:
            if tcp is not None:
                tcp.close()
            if second is not None:
                second.close()


def main():
    executable = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/bin/vpp-cli").resolve()
    for jit in (0, 1):
        for mode in QUERIES:
            run_case(executable, jit, mode)
    print("Structured DNS UDP/TCP, TTL, RCODE, failover, legacy APIs (Interpreter/JIT): PASS")


if __name__ == "__main__":
    main()
