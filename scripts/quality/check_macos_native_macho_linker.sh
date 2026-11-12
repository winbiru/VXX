#!/usr/bin/env bash
# End-to-end gate for the V++ ARM64 MH_OBJECT merger and compiler bootstrap.
# Apple ld is still used for final MH_EXECUTE links, and clang assembles the
# platform runtime once. Neither tool is needed for V++ object merging.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ $# != 2 || "$(uname -s)-$(uname -m)" != Darwin-arm64 ]]; then
    echo 'usage: check_macos_native_macho_linker.sh <verified-native-compiler> <new-output-directory>' >&2
    exit 2
fi
seed="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
if [[ ! -x "$seed" || -e "$2" ]]; then
    echo 'The seed must be executable and the output directory must not exist.' >&2
    exit 2
fi
mkdir -p "$2"
out="$(cd "$2" && pwd)"
sdk="$(xcrun --show-sdk-path)"

clang -arch arm64 -mmacosx-version-min=11.0 \
    -DVPP_NATIVE_VPP_RUNTIME=1 -DVPP_NATIVE_RUNTIME_STACK_ABI=1 \
    -c "$root/scripts/ASM/macos/vpp_aot_primitives_aarch64.S" \
    -o "$out/runtime-asm.o"

export VPP_HOME="$root" VPP_NATIVE_NO_CACHE=1
export VPP_MACOS_DIRECT_LD=1 VPP_NATIVE_MACHO_REL_LINK=1
export VPP_MACOS_SDKROOT="$sdk" VPP_MACOS_RUNTIME_OBJECT="$out/runtime-asm.o"
compiler_source="$root/gói/biên dịch/bản địa/bộ biên dịch.vi"
harness_source="$root/gói/biên dịch/bản địa/kiểm thử liên kết macho.vi"

"$seed" check "$compiler_source"
"$seed" check "$harness_source"
"$seed" build "$harness_source" -o "$out/merger-harness"
python3 "$root/scripts/quality/check_macos_native_macho_inputs.py" \
    "$out/merger-harness" "$out/synthetic"

# Exercise the canonical archive independently of the compiler's own objects.
xcrun ld -arch arm64 -platform_version macos 11.0 11.0 \
    -syslibroot "$sdk" -lSystem "$out/synthetic/good.o" \
    -o "$out/synthetic-exec"
codesign --verify "$out/synthetic-exec"
set +e
"$out/synthetic-exec"
actual=$?
set -e
if [[ "$actual" != 53 ]]; then
    echo "Native synthetic merged object exited $actual, expected 53" >&2
    exit 1
fi
echo 'MACOS_MACHO_SYNTHETIC_EXEC_EXIT=53'

# The callee is defined after the caller, so its relocation must be remapped
# to the later definition before the Apple final executable link.
xcrun ld -arch arm64 -platform_version macos 11.0 11.0 \
    -syslibroot "$sdk" -lSystem "$out/synthetic/definition-after-import.o" \
    -o "$out/synthetic-reversed-exec"
codesign --verify "$out/synthetic-reversed-exec"
set +e
"$out/synthetic-reversed-exec"
actual=$?
set -e
if [[ "$actual" != 53 ]]; then
    echo "Native reversed Mach-O symbol-order executable exited $actual, expected 53" >&2
    exit 1
fi
echo 'MACOS_MACHO_REVERSED_DEFINITION_EXEC_EXIT=53'

cat > "$out/helper.vi" <<'VI'
hàm phụ() { trả về 53; };
VI
cat > "$out/main.vi" <<'VI'
nhập helper.*;
hàm chính() { trả về phụ(); };
VI
cat > "$out/runtime-main.vi" <<'VI'
hàm chính() { trả về 52; };
VI

# Empty SDK/runtime variables ensure source objects never require final-link
# dependencies. Compare the entire output, including symbols and relocations.
VPP_MACOS_SDKROOT= SDKROOT= VPP_MACOS_RUNTIME_OBJECT= \
    "$seed" object "$out/main.vi" -o "$out/source.o"
VPP_MACOS_SDKROOT= SDKROOT= VPP_MACOS_RUNTIME_OBJECT= \
    "$seed" object "$out/main.vi" -o "$out/source-repeat.o"
cmp "$out/source.o" "$out/source-repeat.o"
VPP_MACOS_SDKROOT= SDKROOT= VPP_MACOS_RUNTIME_OBJECT= \
    "$seed" runtime-object "$out/runtime-main.vi" -o "$out/runtime-object.o"
file "$out/source.o" "$out/runtime-object.o" | grep -q 'Mach-O 64-bit object arm64'
echo 'MACOS_MACHO_SOURCE_OBJECT_DETERMINISM=PASS'
echo 'MACOS_MACHO_RUNTIME_OBJECT=PASS'

"$seed" build "$out/main.vi" -o "$out/source-exec"
"$seed" build "$out/runtime-main.vi" -o "$out/runtime-exec"
for name in source runtime; do
    codesign --verify "$out/$name-exec"
    set +e
    "$out/$name-exec"
    actual=$?
    set -e
    if [[ "$name" == source ]]; then
        expected=53
        label=SOURCE
    else
        expected=52
        label=RUNTIME
    fi
    if [[ "$actual" != "$expected" ]]; then
        echo "Native $name executable exited $actual, expected $expected" >&2
        exit 1
    fi
    echo "MACOS_MACHO_${label}_EXEC_EXIT=$expected"
done

# Two generations built through the V++ merger must converge. Normalize only
# LC_UUID and its ad-hoc code signature, both owned by Apple's final linker.
"$seed" build "$compiler_source" -o "$out/stage-next"
"$out/stage-next" check "$compiler_source"
"$out/stage-next" build "$compiler_source" -o "$out/stage-converged"
"$out/stage-converged" check "$compiler_source"
codesign --verify "$out/stage-next"
codesign --verify "$out/stage-converged"
python3 "$root/scripts/quality/check_macos_macho_convergence_inputs.py" \
    "$out/stage-next"
python3 "$root/scripts/quality/check_macos_macho_convergence.py" \
    "$out/stage-next" "$out/stage-converged"

if find "$out" -type f \( -name '*.vpp-module-*' -o -name '*.vpp-exe-*' \) | grep -q .; then
    echo 'Native Mach-O gate left temporary linker files' >&2
    exit 1
fi
echo 'MACOS_MACHO_NATIVE_LINKER_GATE=PASS (relocatable only; executable uses Apple ld)'
