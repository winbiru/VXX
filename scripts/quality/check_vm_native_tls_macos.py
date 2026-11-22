#!/usr/bin/env python3
"""Exercise the native ARM64 TLS socket provider against a local TLS server.

The positive fixture injects a test CA into SecTrust only in its temporary
assembly copy. Production always evaluates the system certificate trust.
Use --negative-only when macOS trustd is inaccessible to the test runner.
"""

import argparse
import platform
import socket
import ssl
import subprocess
import tempfile
import threading
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROVIDER = ROOT / "scripts/ASM/macos/aot_arm64/tls.inc"

CLIENT = r"""
#include <Security/Security.h>
#include <CoreFoundation/CoreFoundation.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

extern OSStatus SSLSetTrustedRoots(SSLContextRef, CFArrayRef, Boolean);

static uint8_t arena[8192] __attribute__((aligned(16)));
uint8_t *vpp_native_memory_base(void) { return arena; }
extern int vpp_native_tls_socket(int, void *);

// Trust the fixture's CA during the SecureTransport handshake. This test-only
// root is also passed to the independent hostname-bound SecTrust evaluation.
int vpp_tls_test_install_roots(SSLContextRef context) {
    const char *path = getenv("VPP_TLS_TEST_ROOT");
    if (!path) return 1;
    FILE *file = fopen(path, "rb");
    if (!file) return 1;
    uint8_t bytes[4096];
    size_t count = fread(bytes, 1, sizeof(bytes), file);
    fclose(file);
    CFDataRef data = CFDataCreate(NULL, bytes, count);
    if (!data) return 1;
    SecCertificateRef cert = SecCertificateCreateWithData(NULL, data);
    CFRelease(data);
    if (!cert) return 1;
    const void *values[] = {cert};
    CFArrayRef anchors = CFArrayCreate(NULL, values, 1, &kCFTypeArrayCallBacks);
    OSStatus status = anchors ? SSLSetTrustedRoots(context, anchors, true) : -1;
    if (anchors) CFRelease(anchors);
    CFRelease(cert);
    return status == errSecSuccess ? 0 : 1;
}

// Called only by the test copy of tls.inc. No test trust anchor is installed
// into the system keychain or referenced by the production provider.
int vpp_tls_test_evaluate(SecTrustRef trust, CFErrorRef *error) {
    const char *path = getenv("VPP_TLS_TEST_ROOT");
    if (!path) return 0;
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    uint8_t bytes[4096];
    size_t count = fread(bytes, 1, sizeof(bytes), file);
    fclose(file);
    CFDataRef data = CFDataCreate(NULL, bytes, count);
    if (!data) return 0;
    SecCertificateRef cert = SecCertificateCreateWithData(NULL, data);
    CFRelease(data);
    if (!cert) return 0;
    const void *values[] = {cert};
    CFArrayRef anchors = CFArrayCreate(NULL, values, 1, &kCFTypeArrayCallBacks);
    OSStatus status = SecTrustSetAnchorCertificates(trust, anchors);
    if (status == errSecSuccess) {
        status = SecTrustSetAnchorCertificatesOnly(trust, true);
    }
    int valid = status == errSecSuccess && SecTrustEvaluateWithError(trust, error);
    CFRelease(anchors);
    CFRelease(cert);
    return valid;
}

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    int fd = atoi(argv[1]);
    signal(SIGPIPE, SIG_IGN);
    struct timeval timeout = {.tv_sec = 2};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    uint8_t ctl[160] __attribute__((aligned(16))) = {0};
    *(int32_t *)(ctl + 0) = fd;
    *(int32_t *)(ctl + 12) = atoi(argv[3]);
    *(int32_t *)(ctl + 16) = 256;
    *(int32_t *)(ctl + 20) = (int32_t)strlen(argv[2]);
    memcpy(arena + 256, argv[2], strlen(argv[2]));

    int upgrade = vpp_native_tls_socket(11, ctl);
    int verified = *(int32_t *)(ctl + 144);
    *(int32_t *)(ctl + 16) = 512;
    *(int32_t *)(ctl + 20) = 5;
    memcpy(arena + 512, "hello", 5);
    int wrote = vpp_native_tls_socket(12, ctl);
    memset(arena + 512, 0, 5);
    // The server delivers the response in distinct TLS application records.
    // Collect each plaintext fragment through the same established session.
    int read_count = 0;
    if (upgrade == 0) {
        while (read_count < 5) {
            *(int32_t *)(ctl + 16) = 512 + read_count;
            *(int32_t *)(ctl + 20) = 5 - read_count;
            int part = vpp_native_tls_socket(13, ctl);
            if (part <= 0) {
                read_count = part;
                break;
            }
            read_count += part;
        }
    } else {
        read_count = vpp_native_tls_socket(13, ctl);
    }
    int valid_payload = read_count == 5 && memcmp(arena + 512, "world", 5) == 0;
    int close_result = vpp_native_tls_socket(14, ctl);
    int second_close = vpp_native_tls_socket(14, ctl);
    printf("upgrade=%d verified=%d sent=%d received=%d payload=%.*s "
           "close=%d second_close=%d\n",
           upgrade, verified, wrote, read_count, read_count > 0 ? read_count : 0,
           arena + 512, close_result, second_close);
    int socket_closed = close(fd);
    int read_after_close = vpp_native_tls_socket(13, ctl);
    if (socket_closed != 0 || read_after_close != -1 ||
        *(int32_t *)(ctl + 144) != 0) return 1;

    int expected_success = strcmp(argv[2], "localhost") == 0 && getenv("VPP_TLS_TEST_ROOT");
    if (expected_success) {
        return upgrade == 0 && verified == 1 && wrote == 5 && valid_payload
            && close_result == 0 && second_close == 0 ? 0 : 1;
    }
    return upgrade == -1 && verified == 0 && wrote == -1 && read_count == -1
        && close_result == 0 && second_close == 0 ? 0 : 1;
}
"""


def run(*args: str) -> None:
    subprocess.run(args, check=True, capture_output=True, text=True)


def make_certificates(work: Path) -> None:
    root_key = work / "root.key"
    root_pem = work / "root.pem"
    leaf_key = work / "leaf.key"
    leaf_csr = work / "leaf.csr"
    leaf_pem = work / "leaf.pem"
    root_der = work / "root.der"
    extensions = work / "leaf.ext"
    extensions.write_text(
        "basicConstraints=critical,CA:FALSE\n"
        "keyUsage=critical,digitalSignature,keyEncipherment\n"
        "extendedKeyUsage=serverAuth\n"
        "subjectAltName=DNS:localhost\n"
    )
    run("openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
        "-days", "1", "-keyout", str(root_key), "-out", str(root_pem),
        "-subj", "/CN=VPP TLS Test Root", "-addext",
        "basicConstraints=critical,CA:TRUE")
    run("openssl", "req", "-newkey", "rsa:2048", "-nodes", "-keyout",
        str(leaf_key), "-out", str(leaf_csr), "-subj", "/CN=localhost")
    run("openssl", "x509", "-req", "-in", str(leaf_csr), "-CA",
        str(root_pem), "-CAkey", str(root_key), "-CAcreateserial",
        "-out", str(leaf_pem), "-days", "1", "-sha256",
        "-extfile", str(extensions))
    run("openssl", "x509", "-in", str(root_pem), "-outform", "DER",
        "-out", str(root_der))


def compile_probe(work: Path, custom_ca: bool) -> Path:
    source = PROVIDER.read_text()
    if custom_ca:
        marker = "    // Enforce one deadline for the complete handshake."
        assert source.count(marker) == 1
        source = source.replace(
            marker,
            "    mov x0, x22\n"
            "    bl _vpp_tls_test_install_roots\n"
            "    cbnz w0, L_tls_fail\n" + marker,
        )
        needle = (
            "    adrp x1, L_tls_evaluate_name@PAGE\n"
            "    add x1, x1, L_tls_evaluate_name@PAGEOFF\n"
            "    bl _dlsym\n"
            "    cbz x0, L_tls_trust_failed\n"
            "    mov x9, x0"
        )
        assert source.count(needle) == 1
        source = source.replace(
            needle,
            "    adrp x9, _vpp_tls_test_evaluate@PAGE\n"
            "    add x9, x9, _vpp_tls_test_evaluate@PAGEOFF"
        )
    tag = "test-ca" if custom_ca else "system-ca"
    asm_file = work / f"{tag}.S"
    c_file = work / f"{tag}.c"
    executable = work / tag
    asm_file.write_text(source)
    c_file.write_text(CLIENT)
    run("clang", "-arch", "arm64", str(c_file), str(asm_file),
        "-framework", "Security", "-framework", "CoreFoundation",
        "-o", str(executable))
    return executable


def exercise(executable: Path, work: Path, hostname: str, *,
             trusted: bool, silent: bool = False, disconnected: bool = False) -> None:
    import os

    client, server = socket.socketpair()
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(str(work / "leaf.pem"), str(work / "leaf.key"))
    errors = []

    def serve() -> None:
        try:
            if disconnected:
                return
            if silent:
                threading.Event().wait(0.5)
                return
            with context.wrap_socket(server, server_side=True) as tls:
                received = tls.recv(5)
                if received == b"hello":
                    for fragment in (b"w", b"or", b"ld"):
                        tls.sendall(fragment)
        except (OSError, ssl.SSLError) as exc:
            errors.append(str(exc))
        finally:
            server.close()

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    env = os.environ.copy()
    if trusted:
        env["VPP_TLS_TEST_ROOT"] = str(work / "root.der")
    else:
        env.pop("VPP_TLS_TEST_ROOT", None)
    try:
        result = subprocess.run(
            [str(executable), str(client.fileno()), hostname,
             "100" if silent else "2000"],
            pass_fds=(client.fileno(),), capture_output=True,
            text=True, env=env, timeout=5
        )
    finally:
        client.close()
        thread.join(timeout=2)
    name = f"{'trusted' if trusted else 'untrusted'} {hostname}"
    if silent:
        name += " timeout"
    if disconnected:
        name += " disconnect"
    print(f"{name}: {result.stdout.strip()}", flush=True)
    if result.returncode != 0:
        raise AssertionError(
            f"{name} returned {result.returncode}: {result.stderr} {errors}"
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--negative-only", action="store_true")
    args = parser.parse_args()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("requires macOS ARM64")
    with tempfile.TemporaryDirectory(prefix="vpp-native-tls-") as folder:
        work = Path(folder)
        make_certificates(work)
        system_ca = compile_probe(work, False)
        exercise(system_ca, work, "localhost", trusted=False)
        exercise(system_ca, work, "localhost", trusted=False, disconnected=True)
        exercise(system_ca, work, "localhost", trusted=False, silent=True)
        if not args.negative_only:
            custom_ca = compile_probe(work, True)
            exercise(custom_ca, work, "localhost", trusted=True)
            exercise(custom_ca, work, "other.local", trusted=True)
    print("V++ native TLS provider: PASS")


if __name__ == "__main__":
    main()
