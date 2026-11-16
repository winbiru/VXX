#!/usr/bin/env bash
# Native i32 ARM64 compiler bootstrap through the V++ executable linker and dyld.
# Clang prepares the platform boundary once; no assembler/linker runs in stages.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ $# != 2 || ! -x "$1" || -e "$2" || "$(uname -s)-$(uname -m)" != Darwin-arm64 ]]; then
    echo 'usage: check_macos_native_executable_selfhosting.sh <verified-compiler> <new-output-dir>' >&2
    exit 2
fi
seed="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
mkdir -p "$2"
out="$(cd "$2" && pwd)"
clang -arch arm64 -mmacosx-version-min=11.0 -DVPP_NATIVE_VPP_RUNTIME=1 \
    -DVPP_NATIVE_RUNTIME_STACK_ABI=1 -c \
    "$root/scripts/ASM/macos/vpp_aot_primitives_aarch64.S" -o "$out/runtime.o"
mkdir "$out/forbid-tools"
export VPP_FORBIDDEN_TOOL_LOG="$out/forbidden-tools.log"
: > "$VPP_FORBIDDEN_TOOL_LOG"
for name in cc c++ gcc g++ clang clang++ ld as lld ld.lld; do
    cat > "$out/forbid-tools/$name" <<'GUARD'
#!/bin/sh
printf '%s\n' "$0 $*" >> "$VPP_FORBIDDEN_TOOL_LOG"
exit 99
GUARD
    chmod +x "$out/forbid-tools/$name"
done
export PATH="$out/forbid-tools:$PATH"
export VPP_HOME="$root" VPP_NATIVE_NO_CACHE=1 VPP_NATIVE_MACHO_EXE_LINK=1
export VPP_NATIVE_MACHO_REL_LINK=1 VPP_MACOS_DIRECT_LD=0
export VPP_MACOS_SDKROOT= SDKROOT= VPP_MACOS_RUNTIME_OBJECT="$out/runtime.o"
compiler="$seed"
for stage in 1 2; do
    "$compiler" build "$root/gói/biên dịch/bản địa/bộ biên dịch.vi" \
        -o "$out/stage$stage" > "$out/stage$stage.log" 2>&1 || {
        cat "$out/stage$stage.log" >&2; exit 1;
    }
    codesign --verify "$out/stage$stage"
    compiler="$out/stage$stage"
done
cmp "$out/stage1" "$out/stage2"
shasum -a 256 "$out/stage1" "$out/stage2"
echo 'MACOS_NATIVE_EXECUTABLE_SELFHOST_EXACT_CMP=PASS'
python3 "$root/scripts/quality/check_native_executable_layout.py" "$out/stage1" "$out/stage2"

# The dynamic AOT compiler consumes the relocatable V++ runtime separately
# from the platform ASM boundary. Require the direct-linked compiler to
# reproduce it across both generations, without the module cache.
runtime_source="$root/gói/máy ảo/runtime AOT.vi"
for stage in 1 2; do
    "$out/stage$stage" runtime-object "$runtime_source" -o "$out/runtime-stage$stage.o"
    test -s "$out/runtime-stage$stage.o"
    file "$out/runtime-stage$stage.o" | grep -q 'Mach-O 64-bit object arm64'
done
cmp "$out/runtime-stage1.o" "$out/runtime-stage2.o"
shasum -a 256 "$out/runtime-stage1.o" "$out/runtime-stage2.o"
echo 'MACOS_NATIVE_RUNTIME_OBJECT_EXACT_CMP=PASS'

printf 'hàm chính() { trả về 52; };\n' > "$out/gói thử nghiệm 𠮷.vi"
"$compiler" build "$out/gói thử nghiệm 𠮷.vi" -o "$out/probe"
set +e
"$out/probe"
actual=$?
set -e
test "$actual" -eq 52
echo 'MACOS_NATIVE_EXECUTABLE_UNICODE_PROBE=PASS'
printf 'hàm chính() { trả về máy đặt quyền tệp mở(-1,448); };\n' > "$out/runtime.vi"
"$compiler" build "$out/runtime.vi" -o "$out/runtime-probe"
set +e
"$out/runtime-probe"
actual=$?
set -e
test "$actual" -eq 255
echo 'MACOS_NATIVE_EXECUTABLE_RUNTIME_PROBE=PASS'
python3 "$root/scripts/quality/check_native_runtime_linker_inputs.py" macho \
    "$compiler" "$out/runtime.o" "$out/input-regressions"

printf 'previous output must survive\n' > "$out/preserved"
cp "$out/preserved" "$out/expected"
printf 'not a Mach-O object\n' > "$out/malformed.o"
if VPP_MACOS_RUNTIME_OBJECT="$out/malformed.o" "$compiler" build \
    "$out/runtime.vi" -o "$out/preserved" > "$out/rejected.log" 2>&1; then
    echo 'Native Mach-O linker accepted a malformed boundary' >&2; exit 1
fi
cmp "$out/preserved" "$out/expected"
if find "$out" -type f \( -name '*.vpp-exe-*' -o -name '*.vpp-module-*' \) | grep -q .; then
    echo 'Native Mach-O linker left temporary files' >&2; exit 1
fi
test ! -s "$VPP_FORBIDDEN_TOOL_LOG"
echo 'MACOS_NATIVE_EXECUTABLE_PATH_TOOL_GUARD=PASS'
echo 'MACOS_NATIVE_EXECUTABLE_SELFHOST=PASS (preassembled ASM; dyld/libSystem; codesign)'
