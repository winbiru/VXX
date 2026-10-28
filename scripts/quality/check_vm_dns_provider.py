#!/usr/bin/env python3
"""Exercise raw resolver opcodes using only the V++ DNS/hosts provider."""
import ipaddress
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading

from check_dns_tcp_fallback import receive_exact


def answer(query, mode):
    qtype = struct.unpack("!H", query[-4:-2])[0]
    if qtype not in (1, 28):
        raise AssertionError("unexpected DNS QTYPE")
    header = query[:2] + b"\x81\x80" + struct.pack("!HHHH", 1, 1, 0, 0)
    address = ipaddress.ip_address("203.0.113.47" if qtype == 1 else "2001:db8::1").packed
    result = header + query[12:] + b"\xc0\x0c" + query[-4:] + struct.pack("!IH", 25, len(address)) + address
    if mode == "id":
        result = bytes([result[0] ^ 1]) + result[1:]
    elif mode == "question":
        result = result[:13] + b"x" + result[14:]
    elif mode == "pointer":
        start = len(header + query[12:])
        result = result[:start] + struct.pack("!H", 0xC000 | start) + result[start + 2:]
    elif mode == "nxdomain":
        result = query[:2] + b"\x81\x83" + struct.pack("!HHHH", 1, 0, 0, 0) + query[12:]
    return result


def execute(vm, image, provider, env, basename=False):
    runtime_env = os.environ.copy()
    for name in list(runtime_env):
        if name.startswith("VPP_DNS_"):
            runtime_env.pop(name)
    runtime_env.update(PATH="", VPP_VM_DNS_PROVIDER=str(provider))
    runtime_env.update(env)
    return subprocess.run([vm.name if basename else str(vm), str(image)],
                          executable=str(vm), cwd="/tmp" if basename else None, env=runtime_env,
                          capture_output=True, text=True, timeout=35, check=False)


def check(result, mode):
    if mode in ("id", "question", "pointer"):
        if result.returncode == 0 or "error=16" not in result.stderr or "DNS VM VI OK" in result.stdout:
            raise AssertionError(f"accepted malformed {mode}: {result}")
        return
    lines = result.stdout.splitlines()
    if result.returncode or not lines or lines[-1] != "DNS VM VI OK":
        raise AssertionError(f"{mode}: {result}")
    addresses = {ipaddress.ip_address(item) for item in lines[:-1]}
    expected = set() if mode == "nxdomain" else {
        ipaddress.ip_address("203.0.113.47"), ipaddress.ip_address("2001:db8::1")}
    if mode == "partial":
        expected = {ipaddress.ip_address("203.0.113.47")}
    if addresses != expected or len(lines) != len(expected) + 1:
        raise AssertionError(f"{mode}: wrong DNS addresses {lines}")


def main():
    vm, image, provider, out = (Path(x).resolve() for x in sys.argv[1:5])
    hosts = out / "dns-provider-hosts.txt"
    hosts.write_text("203.0.113.47 vpp.example\n2001:db8::1 vpp.example\n")
    empty_hosts = out / "dns-provider-empty-hosts.txt"
    empty_hosts.write_text("")
    if sys.argv[5] == "hosts":
        check(execute(vm, image, provider, {"VPP_DNS_VI_HOSTS_FILE": str(hosts)}), "hosts")
        check(execute(vm, image, provider, {"VPP_DNS_VI_HOSTS_FILE": str(hosts),
              "VPP_VM_DNS_PROVIDER": ""}, basename=True), "hosts")
        print("V++ DNS hosts, nested provider/main GC roots, sibling provider with bare argv0/foreign cwd: PASS")
        return 0
    for mode in ("udp", "tcp", "nxdomain", "partial", "failover", "id", "question", "pointer"):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp, socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp:
            try:
                udp.bind(("127.0.0.1", 0))
                port = udp.getsockname()[1]
                tcp.bind(("127.0.0.1", port))
            except PermissionError:
                return 77
            udp.settimeout(15)
            tcp.listen(2)
            tcp.settimeout(15)
            errors = []
            def serve():
                try:
                    for _ in range(1 if mode in ("id", "question", "pointer") else 2):
                        query, peer = udp.recvfrom(4096)
                        if mode == "partial" and struct.unpack("!H", query[-4:-2])[0] == 28:
                            continue
                        if mode != "tcp":
                            udp.sendto(answer(query, mode), peer)
                        else:
                            udp.sendto(query[:2] + b"\x83\x80\x00\x01" + b"\x00" * 6 + query[12:], peer)
                            conn, _ = tcp.accept()
                            with conn:
                                conn.settimeout(10)
                                forwarded = receive_exact(conn, struct.unpack("!H", receive_exact(conn, 2))[0])
                                if forwarded != query:
                                    raise AssertionError("TCP question changed")
                                payload = answer(forwarded, mode)
                                framed = struct.pack("!H", len(payload)) + payload
                                conn.sendall(framed[:1])
                                conn.sendall(framed[1:7])
                                conn.sendall(framed[7:])
                except Exception as exc:
                    errors.append(exc)
            worker = threading.Thread(target=serve, daemon=True)
            worker.start()
            result = execute(vm, image, provider, {
                "VPP_DNS_VI_SERVERS": "127.0.0.2,127.0.0.1" if mode == "failover" else "127.0.0.1",
                "VPP_DNS_VI_PORT": str(port),
                "VPP_DNS_VI_HOSTS_FILE": str(empty_hosts)})
            worker.join(timeout=16)
            if errors or worker.is_alive():
                raise AssertionError(f"{mode}: server {errors}, client {result}")
            check(result, mode)
    print("V++ DNS UDP A/AAAA, fragmented TCP fallback, NXDOMAIN, timeout partial result, server failover, malformed ID/question/pointer: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
