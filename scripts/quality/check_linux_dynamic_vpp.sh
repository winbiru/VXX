#!/usr/bin/env bash
# Build and execute actual V++ dynamic AOT programs in a Linux x86-64 static ELF.
# The separate freestanding syscall probe does not exercise the V++ runtime.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUTPUT_DIR="${1:-$ROOT_DIR/build/native-dynamic-vpp-linux}"
if [[ "$(uname -s)-$(uname -m)" != Linux-x86_64 ]]; then
    echo 'Dynamic V++ freestanding integration requires Linux x86-64.' >&2
    exit 2
fi
for tool in cmake clang readelf nm diff timeout; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing required Linux integration tool: $tool" >&2
        exit 2
    fi
done
# Catch malformed runtime build scripts before spending ~24 minutes in an
# emulated x86-64 Stage 1 bootstrap. Recheck after Stage 1, too: the source
# tree is mounted live from the macOS host during Docker runs.
if ! bash -n "$ROOT_DIR/scripts/build-vpp-aot-runtime.sh"; then
    echo 'Runtime build script has a Bash syntax error.' >&2
    exit 2
fi
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
# AOT codegen emits Intel syntax, while the embedded x86-64 runtime uses
# AT&T syntax. Catch mixed-syntax regressions before a costly Stage 1 build.
printf '.intel_syntax noprefix\n#include "linux/vpp_aot_vpp_runtime_x86_64.inc"\n' | \
    clang -target x86_64-unknown-linux-gnu -x assembler-with-cpp \
        -I"$ROOT_DIR/scripts/ASM" -c - -o "$OUTPUT_DIR/runtime-syntax.o" || {
            echo 'V++ AOT Linux runtime ASM syntax preflight failed.' >&2
            exit 1
        }
STAGE0="$OUTPUT_DIR/stage0/bin/vpp-cli"
NATIVE="$OUTPUT_DIR/nativedriver/vpp-native"
RUNTIME="$OUTPUT_DIR/native-vpp-runtime/runtime.o"
# Docker Desktop may emulate every x86-64 instruction on ARM64. Give the
# compiler bootstrap a separate, configurable budget without relaxing the
# timeouts in native-host compiler tests.
STAGE1_TIMEOUT_SECONDS="${VPP_LINUX_STAGE1_TIMEOUT_SECONDS:-1800}"
if [[ ! "$STAGE1_TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
    echo 'VPP_LINUX_STAGE1_TIMEOUT_SECONDS must be a positive integer.' >&2
    exit 2
fi
RUNTIME_TIMEOUT_SECONDS="${VPP_LINUX_RUNTIME_TIMEOUT_SECONDS:-3600}"
if [[ ! "$RUNTIME_TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
    echo 'VPP_LINUX_RUNTIME_TIMEOUT_SECONDS must be a positive integer.' >&2
    exit 2
fi
export VPP_HOME="$ROOT_DIR"
export VPP_AOT_ROOT="$ROOT_DIR"

# Bootstrapped compilers can use the host C/C++ libraries for now, but their
# generated stack must not be executable. The ELF writer marks GNU-stack
# explicitly; catch regressions in both compiler stages and final fixtures.
check_nonexecutable_stack() {
    local executable="$1" label="$2" flags
    flags="$(readelf -W -l "$executable" | awk '$1 == "GNU_STACK" {print $(NF-1)}')"
    if [[ -z "$flags" || "$flags" == *E* ]]; then
        echo "$label: expected non-executable PT_GNU_STACK, found '${flags:-missing}'." >&2
        return 1
    fi
    echo "$label: PT_GNU_STACK $flags (non-executable)."
}

if [[ ! -f "$OUTPUT_DIR/stage0/CMakeCache.txt" ]]; then
    echo '[1/4] Configure Stage 0 host compiler (only used for bootstrapping)'
    cmake -S "$ROOT_DIR" -B "$OUTPUT_DIR/stage0" \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF \
        -DVPP_ENABLE_LEGACY_LIBFFI=OFF \
        > "$OUTPUT_DIR/stage0-configure.log" 2>&1 || {
            tail -90 "$OUTPUT_DIR/stage0-configure.log" >&2
            exit 1
        }
fi
# Always perform the incremental build. A previous Docker run may have left a
# working Stage 0 executable that predates changes to the native bridge; just
# checking -x would silently reuse that stale compiler on every subsequent run.
echo '[1/4] Update Stage 0 host compiler (incremental)'
cmake --build "$OUTPUT_DIR/stage0" --target vpp-cli --parallel 2 \
    > "$OUTPUT_DIR/stage0-build.log" 2>&1 || {
        tail -110 "$OUTPUT_DIR/stage0-build.log" >&2
        exit 1
    }
if [[ ! -x "$STAGE0" ]]; then
    echo "Stage 0 executable missing after build: $STAGE0" >&2
    exit 1
fi
stage1_needs_build=0
if [[ ! -x "$NATIVE" ]]; then
    stage1_needs_build=1
else
    # Keep successful, expensive emulated builds across retries. Rebuild only
    # when the host seed, Stage 1 driver or V++ native compiler sources change.
    for input in "$STAGE0" "$ROOT_DIR/scripts/quality/checknativedriver.cmake" \
                 "$ROOT_DIR/gói/biên dịch/bản địa/"*.vi; do
        if [[ "$input" -nt "$NATIVE" ]]; then
            stage1_needs_build=1
            echo "Stage 1 out of date: $input"
            break
        fi
    done
fi
if (( stage1_needs_build )); then
    echo "[2/4] Build native V++ Stage 1 compiler for Linux (timeout ${STAGE1_TIMEOUT_SECONDS}s)"
    # Stage 1 is the bootstrap seed. The freestanding runtime object is
    # produced by Stage 1 in the next phase, so it cannot be linked here.
    # CMake captures the child compiler's diagnostics until completion. A
    # heartbeat makes slow QEMU/Rosetta bootstrap distinguishable from Docker
    # stalling at apt-get, without spamming the captured Stage 1 log.
    if ! (
        # Preserve the generated V++ compiler executable after a timeout.
        # Only the large Stage 1 bridge is copied; tiny target probes are not.
        export VPP_NATIVE_STAGE0_KEEP_BRIDGE="$OUTPUT_DIR/nativedriver/stage1-bridge"
        (
            elapsed=0
            while sleep 60; do
                elapsed=$((elapsed + 60))
                echo "[2/4] Stage 1 bootstrap still running (${elapsed}s / ${STAGE1_TIMEOUT_SECONDS}s)" |
                    tee -a "$OUTPUT_DIR/stage1-build.log"
            done
        ) &
        heartbeat_pid=$!
        trap 'kill "$heartbeat_pid" 2>/dev/null || true' EXIT
        VPP_AOT_RUNTIME=cpp VPP_AOT_VPP_OBJECT= \
        cmake -DVPP_EXEC="$STAGE0" -DVPP_ROOT="$ROOT_DIR" \
            -DVPP_TARGET=x86_64-linux-gnu \
            -DVPP_OUTPUT_DIR="$OUTPUT_DIR/nativedriver" \
            -DVPP_STAGE1_TIMEOUT_SECONDS="$STAGE1_TIMEOUT_SECONDS" \
            -DVPP_PREPARE_ONLY=1 \
            -P "$ROOT_DIR/scripts/quality/checknativedriver.cmake" \
            > "$OUTPUT_DIR/stage1-build.log" 2>&1
    ); then
        tail -110 "$OUTPUT_DIR/stage1-build.log" >&2
        exit 1
    fi
fi
check_nonexecutable_stack "$NATIVE" 'V++ Stage 1 compiler'
runtime_needs_build=0
if [[ ! -f "$RUNTIME" ]]; then
    runtime_needs_build=1
elif ! readelf -h "$RUNTIME" 2>/dev/null | grep -Eq 'Type:[[:space:]]+REL '; then
    runtime_needs_build=1
elif [[ "$NATIVE" -nt "$RUNTIME" || "$ROOT_DIR/scripts/build-vpp-aot-runtime.sh" -nt "$RUNTIME" ]]; then
    runtime_needs_build=1
else
    for input in "$ROOT_DIR/gói/máy ảo/"*.vi "$ROOT_DIR/gói/biên dịch/bản địa/"*.vi; do
        if [[ "$input" -nt "$RUNTIME" ]]; then
            runtime_needs_build=1
            break
        fi
    done
fi
if (( runtime_needs_build )); then
    if ! bash -n "$ROOT_DIR/scripts/build-vpp-aot-runtime.sh"; then
        echo 'Runtime build script changed and has a Bash syntax error.' >&2
        exit 2
    fi
    echo "[3/4] Compile dynamic runtime from V++ into Linux ELF relocatable (timeout ${RUNTIME_TIMEOUT_SECONDS}s)"
    if ! (
        (
            elapsed=0
            while sleep 60; do
                elapsed=$((elapsed + 60))
                echo "[3/4] Runtime build still running (${elapsed}s / ${RUNTIME_TIMEOUT_SECONDS}s)"
            done
        ) &
        heartbeat_pid=$!
        trap 'kill "$heartbeat_pid" 2>/dev/null || true' EXIT
        timeout --kill-after=10 "$RUNTIME_TIMEOUT_SECONDS" \
            bash "$ROOT_DIR/scripts/build-vpp-aot-runtime.sh" \
                "$OUTPUT_DIR/native-vpp-runtime" "$NATIVE" \
                > "$OUTPUT_DIR/runtime-build.log" 2>&1
    ); then
            tail -110 "$OUTPUT_DIR/runtime-build.log" >&2
            exit 1
    fi
fi
check_nonexecutable_stack "$OUTPUT_DIR/native-vpp-runtime/vpp-i32" 'V++ Stage 2 i32 compiler'
# Only final user programs use the freestanding linker. Stage 0/1 above
# intentionally retain their temporary host bootstrap dependencies.
export VPP_AOT_RUNTIME=vpp-freestanding
export VPP_AOT_VPP_OBJECT="$RUNTIME"
readelf -h "$RUNTIME" > "$OUTPUT_DIR/runtime-elf-header.txt"
if ! grep -Eq 'Class:[[:space:]]+ELF64' "$OUTPUT_DIR/runtime-elf-header.txt" ||
   ! grep -Eq 'Type:[[:space:]]+REL ' "$OUTPUT_DIR/runtime-elf-header.txt" ||
   ! grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64' "$OUTPUT_DIR/runtime-elf-header.txt"; then
    echo 'V++ runtime object is not ELF64 x86-64 ET_REL.' >&2
    exit 1
fi

cat > "$OUTPUT_DIR/hello.vi" <<'EOF'
hàm chính() { in 42; };
EOF
printf '42\n' > "$OUTPUT_DIR/hello.expected"
cat > "$OUTPUT_DIR/vm.expected" <<'EOF'
-2147483648
-2147483648
-1
3.75
3.0
i
[1, 9, 3]
16
41
42
9
25
switch
ngoại lệ VM
ASM VM OK
EOF
cat > "$OUTPUT_DIR/dynamic.expected" <<'EOF'
42
43
2147483647
3.75
2
🐱
[1, 9, 3]
9
41
43
9
0
1
2
đúng
[ngoại lệ, 42]
44
10
[1, 9, 3]
VPP DYNAMIC AOT OK
EOF

check_fixture() {
    local name="$1" source="$2"
    local executable="$OUTPUT_DIR/$name"
    echo "[4/4] Compile & run V++ dynamic fixture: $name"
    if ! timeout 240 "$STAGE0" --emit-native "$source" "$executable" \
        > "$OUTPUT_DIR/$name.build.log" 2>&1; then
        echo "Linux dynamic fixture $name: AOT compilation/link failed." >&2
        tail -110 "$OUTPUT_DIR/$name.build.log" >&2
        return 1
    fi
    bash "$ROOT_DIR/scripts/quality/audit_linux_static_elf.sh" "$executable" \
        > "$OUTPUT_DIR/$name.audit.log" 2>&1 || {
            cat "$OUTPUT_DIR/$name.audit.log" >&2
            return 1
        }
    if ! timeout 120 "$executable" \
        > "$OUTPUT_DIR/$name.stdout" 2> "$OUTPUT_DIR/$name.stderr"; then
        echo "Linux dynamic fixture $name: execution failed." >&2
        tail -75 "$OUTPUT_DIR/$name.stderr" >&2
        return 1
    fi
    if ! diff -u "$OUTPUT_DIR/$name.expected" "$OUTPUT_DIR/$name.stdout"; then
        echo "Linux dynamic fixture $name: output mismatch." >&2
        return 1
    fi
    echo "Linux dynamic fixture $name: static ELF and V++ output PASS."
}

check_fixture hello "$OUTPUT_DIR/hello.vi"
check_fixture vm "$ROOT_DIR/src/tests/fixtures/vm_asm.vi"
check_fixture dynamic "$ROOT_DIR/src/tests/fixtures/native_dynamic_vpp.vi"
echo 'Linux freestanding V++ dynamic AOT: all tested fixtures PASS.'
