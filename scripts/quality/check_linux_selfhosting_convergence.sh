#!/usr/bin/env bash
# Verify bit-for-bit self-hosting convergence of the native Linux V++ compiler.
# The default mode reuses expensive compiled stages; --build regenerates them.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODE="${1:---verify}"
if [[ "$MODE" == --verify-pair ]]; then
    if [[ $# -ne 4 ]]; then
        echo 'Usage: check_linux_selfhosting_convergence.sh --verify-pair <older-compiler> <newer-compiler> <output-dir>' >&2
        exit 2
    fi
    OLDER_COMPILER="$2"
    NEWER_COMPILER="$3"
    OUTPUT_DIR="$4"
else
    OUTPUT_DIR="${2:-$ROOT_DIR/build/native-dynamic-vpp-linux/convergence-after-elf-symbol-fix}"
    STAGE4="${3:-$(dirname "$OUTPUT_DIR")/stage3-elf-fix/vpp-stage4}"
fi

if [[ "$MODE" != --verify && "$MODE" != --build && "$MODE" != --verify-pair ]]; then
    echo 'Usage: check_linux_selfhosting_convergence.sh [--verify|--build] [output-dir] [stage4-compiler]' >&2
    exit 2
fi
if [[ "$(uname -s)-$(uname -m)" != Linux-x86_64 ]]; then
    echo 'Linux x86-64 is required to execute self-hosted ELF compilers.' >&2
    exit 2
fi
for tool in readelf sha256sum cmp timeout; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing required tool: $tool" >&2
        exit 2
    fi
done
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
export VPP_HOME="$ROOT_DIR"

if [[ "$MODE" == --build ]]; then
    if [[ ! -x "$STAGE4" ]]; then
        echo "Stage 4 compiler missing: $STAGE4" >&2
        exit 2
    fi
    timeout_seconds="${VPP_SELFHOST_STAGE_TIMEOUT_SECONDS:-900}"
    if [[ ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ]]; then
        echo 'VPP_SELFHOST_STAGE_TIMEOUT_SECONDS must be a positive integer.' >&2
        exit 2
    fi
    compiler="$STAGE4"
    for stage in 5 6 7; do
        destination="$OUTPUT_DIR/vpp-stage$stage"
        echo "[selfhost] Compiling Stage $stage with $(basename "$compiler")"
        if ! timeout --kill-after=10 "$timeout_seconds" \
            "$compiler" build "$ROOT_DIR/gói/biên dịch/bản địa/bộ biên dịch linux.vi" \
            -o "$destination" > "$OUTPUT_DIR/stage$stage.log" 2>&1; then
            echo "Stage $stage failed. Last diagnostics:" >&2
            tail -80 "$OUTPUT_DIR/stage$stage.log" >&2
            exit 1
        fi
        compiler="$destination"
    done
fi

if [[ "$MODE" == --verify-pair ]]; then
    compilers=("$OLDER_COMPILER" "$NEWER_COMPILER")
else
    compilers=("$OUTPUT_DIR/vpp-stage5" "$OUTPUT_DIR/vpp-stage6" "$OUTPUT_DIR/vpp-stage7")
fi
for executable in "${compilers[@]}"; do
    if [[ ! -x "$executable" ]]; then
        echo "Missing self-hosted compiler: $executable" >&2
        exit 1
    fi
    header="$(readelf -h "$executable")"
    if ! grep -Eq 'Class:[[:space:]]+ELF64' <<< "$header" ||
       ! grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64' <<< "$header"; then
        echo "Compiler is not an ELF64 x86-64 executable: $executable" >&2
        exit 1
    fi
done

echo '[selfhost] SHA-256:'
sha256sum "${compilers[@]}"
older="${compilers[${#compilers[@]}-2]}"
newer="${compilers[${#compilers[@]}-1]}"
if ! cmp -s "$older" "$newer"; then
    echo 'Consecutive compiler stages differ (self-hosting did not converge).' >&2
    cmp "$older" "$newer" || true
    exit 1
fi
echo 'SELFHOST_EXACT_CMP=0'
if [[ "$MODE" != --verify-pair ]]; then
    echo 'STAGE6_STAGE7_EXACT_CMP=0'
fi

# GCC creates random ccXXXXXX.o object names during linking. Those must not
# leak into local ELF FILE symbols in the reproducible final executables.
for executable in "$older" "$newer"; do
    if readelf -sW "$executable" | \
        awk '$4 == "FILE" {found=1} END {exit !found}'; then
        echo "Compiler contains local STT_FILE symbols: $executable" >&2
        exit 1
    fi
done
echo 'SELFHOST_NO_FILE_SYMBOLS=PASS'
if [[ "$MODE" != --verify-pair ]]; then
    echo 'STAGE6_STAGE7_NO_FILE_SYMBOLS=PASS'
fi

# Compile the smoke program afresh with the newer compiler, so an old output alone
# cannot satisfy the regression gate.
printf 'hàm chính() { trả về 42; };\n' > "$OUTPUT_DIR/selfhost-smoke.vi"
if ! timeout --kill-after=10 120 "$newer" build \
    "$OUTPUT_DIR/selfhost-smoke.vi" -o "$OUTPUT_DIR/selfhost-smoke" \
    > "$OUTPUT_DIR/selfhost-smoke.log" 2>&1; then
    echo 'Converged compiler could not compile the smoke program.' >&2
    tail -80 "$OUTPUT_DIR/selfhost-smoke.log" >&2
    exit 1
fi
set +e
timeout --kill-after=10 30 "$OUTPUT_DIR/selfhost-smoke"
smoke_status=$?
set -e
echo "SELFHOST_SMOKE_EXIT=$smoke_status"
if [[ "$MODE" != --verify-pair ]]; then
    echo "STAGE7_SMOKE_EXIT=$smoke_status"
fi
if [[ "$smoke_status" -ne 42 ]]; then
    echo 'Self-hosted smoke program did not return exit code 42.' >&2
    exit 1
fi

echo '[selfhost] Newer compiler dynamic dependencies (if any):'
readelf -d "$newer" | grep 'NEEDED' || true
if [[ "$MODE" == --verify-pair ]]; then
    # New Linux stages must not regain OpenSSL after switching to SHA256 ASM.
    if readelf -d "$newer" | grep -Eq 'NEEDED.*libcrypto'; then
        echo 'Self-hosted compiler unexpectedly links OpenSSL libcrypto.' >&2
        exit 1
    fi
    echo 'SELFHOST_NO_LIBCRYPTO=PASS'
fi
echo 'SELFHOST_CONVERGENCE=PASS'
