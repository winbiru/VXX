#!/usr/bin/env bash
# Rebuild and verify two consecutive self-hosted Linux compiler stages linked
# with the freestanding V++/ASM host. A preassembled host object is required;
# native V++ ET_EXEC linking is still pending, so GNU ld is used directly.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
mode="${1:---help}"
case "$mode" in
    --build)
        if [[ $# -ne 4 ]]; then
            echo 'Usage: check_linux_freestanding_selfhosting.sh --build <seed-compiler> <host-asm.o> <output-dir>' >&2
            exit 2
        fi
        seed="$2"
        host_asm="$3"
        output_dir="$4"
        ;;
    --verify)
        if [[ $# -ne 4 && $# -ne 5 ]]; then
            echo 'Usage: check_linux_freestanding_selfhosting.sh --verify <stage1> <stage2> <output-dir> [host-asm.o]' >&2
            exit 2
        fi
        older="$2"
        newer="$3"
        output_dir="$4"
        host_asm="${5:-}"
        ;;
    *)
        echo 'Usage: check_linux_freestanding_selfhosting.sh --build <seed-compiler> <host-asm.o> <output-dir>' >&2
        echo '       check_linux_freestanding_selfhosting.sh --verify <stage1> <stage2> <output-dir> [host-asm.o]' >&2
        exit 2
        ;;
esac

if [[ "$(uname -s)-$(uname -m)" != Linux-x86_64 ]]; then
    echo 'Linux x86-64 required for freestanding self-host verification.' >&2
    exit 2
fi
for utility in timeout readelf nm cmp sha256sum; do
    command -v "$utility" >/dev/null || { echo "Missing $utility" >&2; exit 2; }
done
mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd)"
export VPP_HOME="$ROOT_DIR"

if [[ "$mode" == --build ]]; then
    [[ -x "$seed" && -f "$host_asm" ]] || { echo 'Missing seed compiler or freestanding host object.' >&2; exit 2; }
    command -v ld >/dev/null || { echo 'GNU ld required to link bootstrap stages.' >&2; exit 2; }
    compiler="$seed"
    for stage in 1 2; do
        object="$output_dir/freestanding-stage${stage}.o"
        executable="$output_dir/vpp-freestanding-stage${stage}"
        echo "[freestanding selfhost] Stage $stage from $compiler"
        if ! timeout --kill-after=10 "${VPP_SELFHOST_STAGE_TIMEOUT_SECONDS:-900}" \
            "$compiler" runtime-object "$ROOT_DIR/gói/biên dịch/bản địa/bộ biên dịch linux.vi" \
            -o "$object" > "$output_dir/stage${stage}.log" 2>&1; then
            tail -80 "$output_dir/stage${stage}.log" >&2
            exit 1
        fi
        ld -static -x -z noexecstack --build-id=none -e _start \
            "$object" "$host_asm" -o "$executable"
        bash "$ROOT_DIR/scripts/quality/audit_linux_static_elf.sh" "$executable"
        compiler="$executable"
    done
    echo 'FREESTANDING_SELFHOST_LD_ONLY_LINK=PASS'
    older="$output_dir/vpp-freestanding-stage1"
    newer="$output_dir/vpp-freestanding-stage2"
fi

for executable in "$older" "$newer"; do
    [[ -x "$executable" ]] || { echo "Missing compiler: $executable" >&2; exit 1; }
    bash "$ROOT_DIR/scripts/quality/audit_linux_static_elf.sh" "$executable"
    if readelf -sW "$executable" | awk '$4 == "FILE" {found=1} END {exit !found}'; then
        echo "Unexpected ELF FILE symbol in $executable" >&2
        exit 1
    fi
done
sha256sum "$older" "$newer"
cmp "$older" "$newer" || { echo 'Self-hosted freestanding stages differ.' >&2; exit 1; }
echo 'FREESTANDING_SELFHOST_EXACT_CMP=0'

# When the preassembled freestanding host exists, guard the ordinary V++
# `build` command against silently falling back to a C compiler driver.
# This tests the real compiler process boundary, not just ELF dependencies.
host_object="$ROOT_DIR/scripts/ASM/linux/vpp_aot_primitives_linux_x86_64.o"
require_ld_only="${VPP_SELFHOST_REQUIRE_LD_ONLY_BUILD:-0}"
if [[ "$require_ld_only" == 1 && ! -s "$host_object" ]]; then
    echo "Missing $host_object for required GNU ld-only V++ build." >&2
    exit 2
fi
if [[ -s "$host_object" ]]; then
    mkdir -p "$output_dir/forbid-cc"
    printf '#!/bin/sh\necho "unexpected cc invocation" >&2\nexit 99\n' > "$output_dir/forbid-cc/cc"
    chmod +x "$output_dir/forbid-cc/cc"
    export PATH="$output_dir/forbid-cc:$PATH"
    require_ld_only=1
fi

printf 'hàm chính() { trả về 42; };\n' > "$output_dir/selfhost-smoke.vi"
if ! timeout --kill-after=10 120 "$newer" build "$output_dir/selfhost-smoke.vi" \
    -o "$output_dir/selfhost-smoke" > "$output_dir/smoke-build.log" 2>&1; then
    tail -80 "$output_dir/smoke-build.log" >&2
    exit 1
fi
# The default 'build' path must stay freestanding too. A correct exit code
# alone would miss a regression back to glibc/CRT or a dynamic loader.
bash "$ROOT_DIR/scripts/quality/audit_linux_static_elf.sh" "$output_dir/selfhost-smoke"
echo 'FREESTANDING_SELFHOST_DEFAULT_BUILD_STATIC=PASS'
if [[ "$require_ld_only" == 1 ]]; then
    echo 'FREESTANDING_SELFHOST_STANDARD_BUILD_LD_ONLY=PASS'
fi
set +e
timeout --kill-after=5 30 "$output_dir/selfhost-smoke"
smoke_status=$?
set -e
echo "FREESTANDING_SELFHOST_SMOKE_EXIT=$smoke_status"
[[ "$smoke_status" -eq 42 ]] || exit 1

# Compile the same smoke fixture through runtime-object and the freestanding
# host boundary. This catches regressions hidden by the regular hosted build.
if [[ -n "${host_asm:-}" ]]; then
    [[ -f "$host_asm" ]] || { echo "Missing host ASM object: $host_asm" >&2; exit 2; }
    command -v ld >/dev/null || { echo 'GNU ld required to link freestanding smoke.' >&2; exit 2; }
    if ! timeout --kill-after=10 120 "$newer" runtime-object "$output_dir/selfhost-smoke.vi" \
        -o "$output_dir/selfhost-static.o" > "$output_dir/static-smoke-build.log" 2>&1; then
        tail -80 "$output_dir/static-smoke-build.log" >&2
        exit 1
    fi
    ld -static -x -z noexecstack --build-id=none -e _start \
        "$output_dir/selfhost-static.o" "$host_asm" -o "$output_dir/selfhost-static"
    bash "$ROOT_DIR/scripts/quality/audit_linux_static_elf.sh" "$output_dir/selfhost-static"
    set +e
    timeout --kill-after=5 30 "$output_dir/selfhost-static"
    static_status=$?
    set -e
    echo "FREESTANDING_STATIC_SMOKE_EXIT=$static_status"
    [[ "$static_status" -eq 42 ]] || exit 1
fi
echo 'FREESTANDING_SELFHOST_CONVERGENCE=PASS'
