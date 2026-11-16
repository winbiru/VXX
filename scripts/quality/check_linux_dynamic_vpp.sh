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
if [[ $# -ne 3 ]]; then
    echo 'Usage: check_linux_dynamic_vpp.sh <output-dir> <full-vpp-native> <runtime.o>' >&2
    exit 2
fi
for tool in clang readelf nm diff timeout; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing required Linux integration tool: $tool" >&2
        exit 2
    fi
done
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
NATIVE="$(realpath "$2")"
RUNTIME="$(realpath "$3")"
if [[ ! -x "$NATIVE" || ! -s "$RUNTIME" ]]; then
    echo 'Missing full native V++ compiler or V++ runtime object.' >&2
    exit 2
fi
export VPP_HOME="$ROOT_DIR" VPP_AOT_VPP_OBJECT="$RUNTIME"
export VPP_AOT_ASSEMBLER="$(command -v clang)"
bash "$ROOT_DIR/scripts/quality/audit_linux_static_elf.sh" "$NATIVE"
readelf -h "$RUNTIME" > "$OUTPUT_DIR/runtime-elf-header.txt"
if ! grep -Eq 'Class:[[:space:]]+ELF64' "$OUTPUT_DIR/runtime-elf-header.txt" ||
   ! grep -Eq 'Type:[[:space:]]+REL ' "$OUTPUT_DIR/runtime-elf-header.txt" ||
   ! grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64' "$OUTPUT_DIR/runtime-elf-header.txt"; then
    echo 'V++ runtime object is not ELF64 x86-64 ET_REL.' >&2
    exit 1
fi
"$NATIVE" --version
# Never use a stale C++ stage0 executable. Build each fixture through the
# full V++ compiler and verify the actual program on Linux.
BUILD_TIMEOUT_SECONDS="${VPP_LINUX_FIXTURE_TIMEOUT_SECONDS:-600}"

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
    local expected_status="${3:-0}" status=0
    local executable="$OUTPUT_DIR/$name"
    echo "Compile & run V++ dynamic fixture: $name"
    if ! timeout "$BUILD_TIMEOUT_SECONDS" "$NATIVE" build "$source" -o "$executable" \
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
    timeout 120 "$executable" \
        > "$OUTPUT_DIR/$name.stdout" 2> "$OUTPUT_DIR/$name.stderr" || status=$?
    if [[ "$status" != "$expected_status" ]]; then
        echo "Linux dynamic fixture $name: exit $status, expected $expected_status." >&2
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
check_fixture vm "$ROOT_DIR/scripts/test/fixtures/vm_asm.vi"
check_fixture dynamic "$ROOT_DIR/scripts/test/fixtures/native_dynamic_vpp.vi"
cp "$ROOT_DIR/scripts/test/expected/native_aot_private_method.expected" "$OUTPUT_DIR/private-method.expected"
check_fixture private-method "$ROOT_DIR/scripts/test/fixtures/native_aot_private_method.vi"
cp "$ROOT_DIR/scripts/test/expected/native_i32_direct_calls.expected" "$OUTPUT_DIR/direct-calls.expected"
check_fixture direct-calls "$ROOT_DIR/scripts/test/fixtures/native_i32_direct_calls.vi"
cp "$ROOT_DIR/scripts/test/expected/native_loop_stack_gc.expected" "$OUTPUT_DIR/loop-stack-gc.expected"
check_fixture loop-stack-gc "$ROOT_DIR/scripts/test/fixtures/native_loop_stack_gc.vi"
cp "$ROOT_DIR/scripts/test/expected/native_return_semantics.expected" "$OUTPUT_DIR/return-semantics.expected"
check_fixture return-semantics "$ROOT_DIR/scripts/test/fixtures/native_return_semantics.vi" 42
: > "$OUTPUT_DIR/exit-scalar.expected"
check_fixture exit-scalar "$ROOT_DIR/scripts/test/fixtures/native_exit_scalar.vi" 42
cp "$ROOT_DIR/scripts/test/expected/native_protected_loop.expected" "$OUTPUT_DIR/protected-loop.expected"
check_fixture protected-loop "$ROOT_DIR/scripts/test/fixtures/native_protected_loop.vi"
cp "$ROOT_DIR/scripts/test/expected/native_frame_pool_gc.expected" "$OUTPUT_DIR/frame-pool-gc.expected"
check_fixture frame-pool-gc "$ROOT_DIR/scripts/test/fixtures/native_frame_pool_gc.vi"
cp "$ROOT_DIR/scripts/test/expected/native_linux_heap_capacity.expected" "$OUTPUT_DIR/heap-capacity.expected"
check_fixture heap-capacity "$ROOT_DIR/scripts/test/fixtures/native_linux_heap_capacity.vi"
echo 'Linux freestanding V++ dynamic AOT: all tested fixtures PASS.'
