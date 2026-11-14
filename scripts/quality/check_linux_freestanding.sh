#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUTPUT_DIR="${1:-$ROOT_DIR/build/native-freestanding-smoke}"
if [[ "$(uname -s)" != Linux || "$(uname -m)" != x86_64 ]]; then
    echo 'Linux x86-64 is required to link and execute the freestanding smoke test.' >&2
    exit 2
fi
if [[ -z "${CC:-}" ]]; then
    if command -v clang >/dev/null 2>&1; then
        CC=clang
    else
        CC=cc
    fi
fi
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"

"$CC" -c -o "$OUTPUT_DIR/start.o" "$ROOT_DIR/scripts/ASM/linux/vpp_linux_start_x86_64.S"
"$CC" -c -o "$OUTPUT_DIR/syscalls.o" "$ROOT_DIR/scripts/ASM/linux/vpp_linux_syscalls_x86_64.S"
"$CC" -c -o "$OUTPUT_DIR/runtime-core.o" "$ROOT_DIR/scripts/ASM/linux/vpp_linux_runtime_core_x86_64.S"
"$CC" -c -o "$OUTPUT_DIR/directory.o" "$ROOT_DIR/scripts/ASM/linux/vpp_linux_directory_x86_64.S" \
    "-I$ROOT_DIR/scripts/ASM"
"$CC" -c -o "$OUTPUT_DIR/sha256.o" "$ROOT_DIR/scripts/ASM/linux/vpp_linux_sha256_x86_64.S"
"$CC" -c -o "$OUTPUT_DIR/numeric.o" "$ROOT_DIR/scripts/ASM/linux/vpp_linux_f64_x86_64.S"
"$CC" -c -o "$OUTPUT_DIR/process.o" "$ROOT_DIR/scripts/ASM/linux/vpp_linux_process_x86_64.S"
"$CC" -c -o "$OUTPUT_DIR/socket.o" "$ROOT_DIR/scripts/ASM/linux/vpp_linux_socket_x86_64.S"
"$CC" -c -o "$OUTPUT_DIR/probe.o" "$ROOT_DIR/scripts/quality/native_freestanding_linux_probe.S"
"$CC" -c -o "$OUTPUT_DIR/socket-probe.o" "$ROOT_DIR/scripts/quality/native_freestanding_linux_socket_probe.S"
"$CC" -nostdlib -static -no-pie -Wl,-e,_start -Wl,--build-id=none \
    -o "$OUTPUT_DIR/vpp-freestanding-probe" \
    "$OUTPUT_DIR/start.o" "$OUTPUT_DIR/syscalls.o" \
    "$OUTPUT_DIR/runtime-core.o" "$OUTPUT_DIR/directory.o" \
    "$OUTPUT_DIR/sha256.o" "$OUTPUT_DIR/process.o" "$OUTPUT_DIR/socket.o" \
    "$OUTPUT_DIR/probe.o" "$OUTPUT_DIR/socket-probe.o" "$OUTPUT_DIR/numeric.o"

bash "$ROOT_DIR/scripts/quality/audit_linux_static_elf.sh" "$OUTPUT_DIR/vpp-freestanding-probe"

if actual="$(cd "$OUTPUT_DIR" && HOME=/tmp PATH="$OUTPUT_DIR" VPP_FREESTANDING_PROBE=ready ./vpp-freestanding-probe check argv)"; then
    :
else
    status=$?
    echo "Freestanding ELF probe failed to execute or exited with status $status: $actual" >&2
    exit "$status"
fi
if [[ "$actual" != 'V++ Linux ELF freestanding: OK' ]]; then
    echo "Unexpected probe output: $actual" >&2
    exit 1
fi
if (cd "$OUTPUT_DIR" && HOME=/tmp VPP_FREESTANDING_PROBE=ready ./vpp-freestanding-probe only-one-argument); then
    echo 'The probe accepted an invalid argument count' >&2
    exit 1
else
    status=$?
    if [[ "$status" -ne 10 ]]; then
        echo "Wrong argument-check status: $status (expected 10)" >&2
        exit 1
    fi
fi
for leftover in .vpp-freestanding-syscall-probe.tmp \
    .vpp-freestanding-native-probe.tmp \
    .vpp-freestanding-native-probe-renamed.tmp \
    .vpp-freestanding-native-probe-dir; do
    if [[ -e "$OUTPUT_DIR/$leftover" ]]; then
        echo "The probe failed to remove $leftover" >&2
        exit 1
    fi
done
if compgen -G "$OUTPUT_DIR/.vpp-freestanding-mkstemp-*" >/dev/null; then
    echo 'The probe failed to remove a temporary file' >&2
    exit 1
fi
echo 'Freestanding ELF: no interpreter, no needed libraries, no unresolved symbols; syscalls OK.'
