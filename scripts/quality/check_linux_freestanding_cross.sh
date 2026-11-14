#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUTPUT_DIR="${1:-$ROOT_DIR/build/native-freestanding-cross}"
CC="${CC:-clang}"
if ! command -v "$CC" >/dev/null 2>&1; then
    echo "A Clang assembler is required: $CC" >&2
    exit 2
fi
if ! command -v nm >/dev/null 2>&1; then
    echo 'nm is required to verify the freestanding object symbol closure.' >&2
    exit 2
fi
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"

"$CC" -target x86_64-unknown-linux-gnu -c \
    "$ROOT_DIR/scripts/ASM/linux/vpp_linux_start_x86_64.S" -o "$OUTPUT_DIR/start.o"
"$CC" -target x86_64-unknown-linux-gnu -c \
    "$ROOT_DIR/scripts/ASM/linux/vpp_linux_syscalls_x86_64.S" -o "$OUTPUT_DIR/syscalls.o"
"$CC" -target x86_64-unknown-linux-gnu -c \
    "$ROOT_DIR/scripts/ASM/linux/vpp_linux_runtime_core_x86_64.S" -o "$OUTPUT_DIR/runtime-core.o"
"$CC" -target x86_64-unknown-linux-gnu -c \
    "$ROOT_DIR/scripts/ASM/linux/vpp_linux_directory_x86_64.S" \
    "-I$ROOT_DIR/scripts/ASM" -o "$OUTPUT_DIR/directory.o"
"$CC" -target x86_64-unknown-linux-gnu -c \
    "$ROOT_DIR/scripts/ASM/linux/vpp_linux_sha256_x86_64.S" -o "$OUTPUT_DIR/sha256.o"
"$CC" -target x86_64-unknown-linux-gnu -c \
    "$ROOT_DIR/scripts/ASM/linux/vpp_linux_process_x86_64.S" -o "$OUTPUT_DIR/process.o"
"$CC" -target x86_64-unknown-linux-gnu -c \
    "$ROOT_DIR/scripts/ASM/linux/vpp_linux_socket_x86_64.S" -o "$OUTPUT_DIR/socket.o"
"$CC" -target x86_64-unknown-linux-gnu -c \
    "$ROOT_DIR/scripts/quality/native_freestanding_linux_probe.S" -o "$OUTPUT_DIR/probe.o"
"$CC" -target x86_64-unknown-linux-gnu -c \
    "$ROOT_DIR/scripts/quality/native_freestanding_linux_socket_probe.S" -o "$OUTPUT_DIR/socket-probe.o"
"$CC" -target x86_64-unknown-linux-gnu -c \
    "$ROOT_DIR/scripts/ASM/linux/vpp_linux_f64_x86_64.S" \
    "-I$ROOT_DIR/scripts/ASM" -o "$OUTPUT_DIR/numeric.o"

# Native numeric operations only depend on the arena entry point supplied by
# runtime-core. Decimal parsing/formatting is compiled from V++ separately.
numeric_undefined="$(nm -u "$OUTPUT_DIR/numeric.o" | awk 'NF {print $NF}')"
if [[ "$numeric_undefined" != 'vpp_native_memory_base' ]]; then
    echo "Unexpected numeric object imports: $numeric_undefined" >&2
    exit 1
fi
for numeric_export in vpp_native_f64_from_i32 vpp_native_f64_binary \
    vpp_native_f64_compare vpp_native_f64_to_i32; do
    if ! nm -g "$OUTPUT_DIR/numeric.o" | grep -q " T $numeric_export$"; then
        echo "Missing numeric primitive: $numeric_export" >&2
        exit 1
    fi
done

sha_undefined="$(nm -u "$OUTPUT_DIR/sha256.o" | awk 'NF {print $NF}')"
if [[ "$sha_undefined" != 'vpp_native_memory_base' ]]; then
    echo "Unexpected SHA-256 object imports: $sha_undefined" >&2
    exit 1
fi
for sha_export in vpp_native_sha_begin vpp_native_sha_update vpp_native_sha_end; do
    if ! nm -g "$OUTPUT_DIR/sha256.o" | grep -q " T $sha_export$"; then
        echo "Missing freestanding SHA-256 function: $sha_export" >&2
        exit 1
    fi
done
process_undefined="$(nm -u "$OUTPUT_DIR/process.o" | awk 'NF {print $NF}')"
if [[ "$process_undefined" == *'@PLT'* ]] || nm -u "$OUTPUT_DIR/process.o" | grep -E '(posix_spawn|__errno_location|pipe@|poll@|waitpid@)' >/dev/null; then
    echo "Unexpected POSIX/libc imports in the process provider: $process_undefined" >&2
    exit 1
fi
if ! nm -g "$OUTPUT_DIR/process.o" | grep -q ' T vpp_native_os_process$'; then
    echo 'Missing Linux freestanding process provider' >&2
    exit 1
fi
socket_undefined="$(nm -u "$OUTPUT_DIR/socket.o" | awk 'NF {print $NF}')"
if [[ "$socket_undefined" == *'@PLT'* ]] || nm -u "$OUTPUT_DIR/socket.o" | grep -E '(__errno_location|socket@|connect@|setsockopt@)' >/dev/null; then
    echo "Unexpected libc imports in Linux socket provider: $socket_undefined" >&2
    exit 1
fi
if ! nm -g "$OUTPUT_DIR/socket.o" | grep -q ' T vpp_native_os_socket$'; then
    echo 'Missing Linux freestanding socket provider' >&2
    exit 1
fi

# Verify the freestanding probe's ELF objects form a closed set, without needing a
# Linux linker on the cross-compilation host. Link and run on Linux separately.
defined="$OUTPUT_DIR/.freestanding-defined.tmp"
referenced="$OUTPUT_DIR/.freestanding-referenced.tmp"
unresolved="$OUTPUT_DIR/.freestanding-unresolved.tmp"
trap 'rm -f "$defined" "$referenced" "$unresolved"' EXIT
: > "$defined"
: > "$referenced"
for object in start syscalls runtime-core directory sha256 process socket probe socket-probe numeric; do
    nm -g "$OUTPUT_DIR/$object.o" | awk \
        'NF >= 2 && $(NF-1) != "U" && $(NF-1) ~ /^[A-Za-z]$/ { print $NF }' \
        >> "$defined"
    nm -u "$OUTPUT_DIR/$object.o" | awk 'NF >= 2 { print $NF }' \
        >> "$referenced"
done
LC_ALL=C sort -u -o "$defined" "$defined"
LC_ALL=C sort -u -o "$referenced" "$referenced"
LC_ALL=C comm -23 "$referenced" "$defined" > "$unresolved"
if [[ -s "$unresolved" ]]; then
    echo 'Unresolved references across freestanding ELF objects:' >&2
    cat "$unresolved" >&2
    exit 1
fi
echo 'Linux freestanding x86-64 ELF objects: cross-assembly and symbol closure OK.'
bash "$ROOT_DIR/scripts/quality/check_linux_aot_runtime_bridge.sh" "$OUTPUT_DIR"
