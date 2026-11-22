#!/usr/bin/env python3
"""Exercise the Windows x86-64 Schannel client against a loopback TLS server."""

import argparse
import os
import platform
import socket
import ssl
import subprocess
import tempfile
import threading
from pathlib import Path

from check_vm_native_tls_linux import ROOT, certificates

ASM = ROOT / "scripts/ASM/windows/aot_x86_64"


CLIENT = r"""
#define SECURITY_WIN32
#include <winsock2.h>
#include <windows.h>
#include <wincrypt.h>
#include <schannel.h>
#include <sspi.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char arena[135271424] __attribute__((aligned(16)));
static HCERTCHAINENGINE test_engine;
_Static_assert(offsetof(CERT_CHAIN_PARA, RequestedUsage.Usage.cUsageIdentifier) == 16,
               "unexpected Windows x64 certificate usage layout");
_Static_assert(offsetof(CERT_CHAIN_PARA, RequestedUsage.Usage.rgpszUsageIdentifier) == 24,
               "unexpected Windows x64 certificate OID pointer layout");
_Static_assert(offsetof(CERT_CONTEXT, hCertStore) == 32,
               "unexpected Windows x64 peer certificate layout");
_Static_assert(offsetof(CERT_CHAIN_CONTEXT, TrustStatus.dwErrorStatus) == 4,
               "unexpected Windows x64 chain trust status layout");
extern int vpp_native_os_socket(int mode, int offset);
unsigned char *vpp_native_memory_base(void) { return arena; }

// Schannel normally also checks the system roots during negotiation. For the
// isolated fixture, defer that check to the provider's explicit SSL policy.
SECURITY_STATUS test_acquire_credentials(
    SEC_WCHAR *principal, SEC_WCHAR *package, ULONG use, void *id,
    void *auth, SEC_GET_KEY_FN get_key, void *argument, PCredHandle credential,
    PTimeStamp expiry) {
    SCHANNEL_CRED *settings = auth;
    if (test_engine) {
        settings->dwFlags &= ~SCH_CRED_AUTO_CRED_VALIDATION;
        settings->dwFlags |= SCH_CRED_MANUAL_CRED_VALIDATION;
    }
    return AcquireCredentialsHandleW(principal, package, use, id, auth, get_key,
                                     argument, credential, expiry);
}

BOOL test_certificate_chain(HCERTCHAINENGINE engine, PCCERT_CONTEXT cert,
    LPFILETIME time, HCERTSTORE extra, PCERT_CHAIN_PARA parameters, DWORD flags,
    LPVOID reserved, PCCERT_CHAIN_CONTEXT *chain) {
    if (test_engine) {
        engine = test_engine;
        // The ephemeral CA has no published CRL.
        flags &= ~CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
    }
    return CertGetCertificateChain(engine, cert, time, extra, parameters, flags,
                                   reserved, chain);
}

static void put_i32(int offset, int32_t value) {
    memcpy(arena + 64 + offset, &value, sizeof(value));
}

static int32_t get_i32(int offset) {
    int32_t value;
    memcpy(&value, arena + 64 + offset, sizeof(value));
    return value;
}

static int make_chain_engine(const char *root_hex) {
    BYTE der[4096];
    size_t n = strlen(root_hex) / 2;
    if (n == 0 || n > sizeof(der)) return 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned int value;
        if (sscanf(root_hex + i * 2, "%2x", &value) != 1) return 0;
        der[i] = (BYTE)value;
    }
    HCERTSTORE roots = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
    if (!roots) return 0;
    CERT_CHAIN_ENGINE_CONFIG config = {0};
    config.cbSize = sizeof(config);
    config.hExclusiveRoot = roots;
    int ok = CertAddEncodedCertificateToStore(roots, X509_ASN_ENCODING, der,
        (DWORD)n, CERT_STORE_ADD_ALWAYS, NULL);
    if (ok) {
        ok = CertCreateCertificateChainEngine(&config, &test_engine);
        if (!ok && GetLastError() == (DWORD)E_INVALIDARG) {
            // Older Wine accepts the Windows 7 layout with hExclusiveRoot.
            config.cbSize = offsetof(CERT_CHAIN_ENGINE_CONFIG, dwExclusiveFlags);
            ok = CertCreateCertificateChainEngine(&config, &test_engine);
        }
    }
    if (!ok) fprintf(stderr, "test certificate chain engine setup failed: %lu (size %lu)\n",
                     GetLastError(), config.cbSize);
    CertCloseStore(roots, 0);
    return ok;
}

int main(int argc, char **argv) {
    if (argc != 6) return 2;
    if (*argv[5] && !make_chain_engine(argv[5])) return 3;
    put_i32(8, atoi(argv[1]));
    put_i32(12, atoi(argv[3]));
    put_i32(24, 4);
    memcpy(arena + 64 + 32, "\x7f\0\0\x01", 4);
    if (vpp_native_os_socket(0, 64) <= 0) return 4;

    put_i32(16, 512);
    put_i32(20, (int32_t)strlen(argv[2]));
    memcpy(arena + 512, argv[2], strlen(argv[2]));
    int upgrade = vpp_native_os_socket(11, 64);
    int verified = get_i32(144);
    put_i32(16, 768);
    put_i32(20, 5);
    memcpy(arena + 768, "hello", 5);
    int written = vpp_native_os_socket(12, 64);
    memset(arena + 768, 0, 5);
    int received = 0;
    if (upgrade == 0) {
        while (received < 5) {
            put_i32(16, 768 + received);
            put_i32(20, 5 - received);
            int part = vpp_native_os_socket(13, 64);
            if (part <= 0) {
                received = part;
                break;
            }
            received += part;
        }
    } else {
        received = vpp_native_os_socket(13, 64);
    }
    int payload = received == 5 && memcmp(arena + 768, "world", 5) == 0;
    int release = vpp_native_os_socket(14, 64);
    int release_again = vpp_native_os_socket(14, 64);
    int close = vpp_native_os_socket(5, 64);
    int read_after_close = vpp_native_os_socket(4, 64);
    if (test_engine) CertFreeCertificateChainEngine(test_engine);

    printf("upgrade=%d verified=%d write=%d read=%d world=%d "
           "release=%d release_again=%d close=%d read_after_close=%d\n",
           upgrade, verified, written, received, payload, release, release_again,
           close, read_after_close);
    if (release != 0 || release_again != 0 || get_i32(144) != 0 ||
        close != 0 || read_after_close >= 0) return 5;
    if (atoi(argv[4]))
        return upgrade == 0 && verified == 1 && written == 5 && payload ? 0 : 1;
    return upgrade < 0 && verified == 0 && written < 0 && received < 0 ? 0 : 1;
}
"""


def compile_probe(work, compiler):
    asm = work / "socket.S"
    asm.write_text(
        '#define VPP_ARENA_SIZE 135271424\n'
        '#include "he_thong.inc"\n'
        '#include "o_cam.inc"\n'
        '#define AcquireCredentialsHandleW test_acquire_credentials\n'
        '#define CertGetCertificateChain test_certificate_chain\n'
        '#include "tls.inc"\n'
    )
    client = work / "client.c"
    client.write_text(CLIENT)
    executable = work / "client.exe"
    subprocess.run(
        [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
         "-D_WIN32_WINNT=0x0A00", "-DNTDDI_VERSION=0x0A000000", f"-I{ASM}",
         str(client), str(asm), "-o", str(executable), "-lsecur32", "-lcrypt32",
         "-lws2_32", "-liphlpapi", "-lbcrypt", "-lntdll"],
        check=True,
    )
    return executable


def exercise(executable, context, root_hex, hostname="localhost", mode="tls", wine=None):
    errors = []
    trusted = bool(root_hex)
    success = trusted and hostname == "localhost" and mode == "tls"

    with socket.socket() as server:
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        server.settimeout(15)

        def serve():
            try:
                client, _ = server.accept()
                with client:
                    client.settimeout(5)
                    if mode == "disconnect":
                        return
                    if mode == "timeout":
                        threading.Event().wait(0.5)
                        return
                    with context.wrap_socket(client, server_side=True) as tls:
                        if tls.recv(5) == b"hello":
                            for fragment in (b"w", b"or", b"ld"):
                                tls.sendall(fragment)
            except OSError as error:
                errors.append(error)

        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        command = [str(executable)]
        if wine:
            command.insert(0, wine)
        result = subprocess.run(
            [*command, str(server.getsockname()[1]), hostname, "100" if mode == "timeout"
             else "4000", str(int(success)), root_hex],
            capture_output=True, text=True, timeout=30,
        )
        thread.join(timeout=5)

    label = f"{'trusted' if trusted else 'system trust'} {hostname} {mode}"
    print(f"{label}: {result.stdout.strip()}", flush=True)
    if result.returncode or thread.is_alive() or (success and errors):
        raise AssertionError(f"{label} failed ({result.returncode}): {result.stderr} {errors}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--wine", help="run the x86-64 Windows probe under Wine")
    parser.add_argument("--negative-only", action="store_true")
    args = parser.parse_args()
    if not args.wine and (platform.system() != "Windows" or
                          platform.machine().lower() not in ("amd64", "x86_64")):
        parser.error("Windows x86-64 or --wine is required")

    compiler = os.environ.get("CC", "x86_64-w64-mingw32-gcc" if args.wine else "gcc")
    with tempfile.TemporaryDirectory(prefix="vpp-windows-tls-") as directory:
        work = Path(directory)
        executable = compile_probe(work, compiler)
        certificates(work)
        root_hex = subprocess.check_output(
            ["openssl", "x509", "-in", str(work / "root.pem"), "-outform", "DER"]
        ).hex()
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(work / "leaf.pem", work / "leaf.key")
        exercise(executable, context, "", wine=args.wine)
        exercise(executable, context, "", mode="disconnect", wine=args.wine)
        exercise(executable, context, "", mode="timeout", wine=args.wine)
        if not args.negative_only:
            exercise(executable, context, root_hex, wine=args.wine)
            exercise(executable, context, root_hex, hostname="other.local", wine=args.wine)
    scope = "negative checks" if args.negative_only else "checks"
    print(f"Windows x86-64 Schannel {scope} passed")


if __name__ == "__main__":
    main()
