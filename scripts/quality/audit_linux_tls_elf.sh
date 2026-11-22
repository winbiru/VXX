#!/usr/bin/env bash
# The OpenSSL-backed Linux AOT runtime requires a dynamic loader and trusted
# system certificate store. Assert the executable has the intended dependencies.
set -euo pipefail
if [[ $# -ne 1 || ! -f "$1" ]]; then
    echo 'Usage: audit_linux_tls_elf.sh <linux-executable>' >&2
    exit 2
fi
for utility in readelf; do
    command -v "$utility" >/dev/null || { echo "Missing tool: $utility" >&2; exit 2; }
done
header="$(readelf -h "$1")"
if ! grep -q 'Class:.*ELF64' <<< "$header" ||
   ! grep -q 'Machine:.*Advanced Micro Devices X86-64' <<< "$header"; then
    echo 'Expected Linux x86-64 ELF64 output.' >&2
    exit 1
fi
headers="$(readelf -W -l "$1")"
stack="$(awk '$1 == "GNU_STACK" {print $(NF-1)}' <<< "$headers")"
if [[ -z "$stack" || "$stack" == *E* ]] || ! grep -q ' INTERP ' <<< "$headers"; then
    echo 'TLS executable needs a dynamic ELF interpreter and non-executable stack.' >&2
    exit 1
fi
dynamic="$(readelf -d "$1")"
for dependency in libssl.so libcrypto.so libc.so; do
    if ! grep -Fq "$dependency" <<< "$dynamic"; then
        echo "Missing required TLS dependency: $dependency" >&2
        exit 1
    fi
done
if grep -Eq '\((RPATH|RUNPATH|TEXTREL)\)' <<< "$dynamic"; then
    echo 'TLS executable contains an unsafe dynamic loader path or text relocations.' >&2
    exit 1
fi
echo 'Linux TLS ELF audit: x86-64, dynamic OpenSSL and libc, NX stack, no loader overrides.'
