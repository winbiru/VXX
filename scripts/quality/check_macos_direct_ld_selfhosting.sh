#!/usr/bin/env bash
# Bootstrap the ARM64 i32 compiler through Apple ld instead of the clang
# linker driver, including multi-module relocatable links. Platform ASM is
# assembled once into a temporary object for executable links;
# the compiler still depends on Apple's linker, SDK, dyld and libSystem.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ $# != 2 || "$(uname -s)-$(uname -m)" != Darwin-arm64 ]]; then
    echo 'usage: check_macos_direct_ld_selfhosting.sh <current-native-compiler> <new-output-directory>' >&2
    exit 2
fi
seed="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
if [[ ! -x "$seed" || -e "$2" ]]; then
    echo 'Compiler must be executable and output directory must not exist.' >&2
    exit 2
fi

mkdir -p "$2"
out="$(cd "$2" && pwd)"
sdk="$(xcrun --show-sdk-path)"
test -d "$sdk"

# Clang acts only as a one-time assembler for this target; V++ owns all
# source-object generation and invokes /usr/bin/ld for both link modes.
clang -arch arm64 -mmacosx-version-min=11.0 \
    -DVPP_NATIVE_VPP_RUNTIME=1 -DVPP_NATIVE_RUNTIME_STACK_ABI=1 \
    -c "$root/scripts/ASM/macos/vpp_aot_primitives_aarch64.S" \
    -o "$out/runtime.o"

export VPP_HOME="$root" VPP_NATIVE_NO_CACHE=1 VPP_MACOS_DIRECT_LD=1
export VPP_MACOS_SDKROOT="$sdk" VPP_MACOS_RUNTIME_OBJECT="$out/runtime.o"
source="$root/gói/biên dịch/bản địa/bộ biên dịch.vi"
printf 'hàm chính() { trả về 52; };\n' > "$out/minimal.vi"

"$seed" build "$source" -o "$out/stage1"
"$out/stage1" check "$out/minimal.vi"
"$out/stage1" build "$source" -o "$out/stage2"
"$out/stage2" check "$out/minimal.vi"
python3 "$root/scripts/quality/check_macos_macho_convergence.py" \
    "$out/stage1" "$out/stage2"

for stage in 1 2; do
    codesign --verify "$out/stage$stage"
    "$out/stage$stage" build "$out/minimal.vi" -o "$out/probe$stage"
    set +e
    "$out/probe$stage"
    status=$?
    set -e
    if [[ "$status" != 52 ]]; then
        echo "Direct ld Stage $stage probe exited $status, expected 52" >&2
        exit 1
    fi
done
echo 'MACOS_DIRECT_LD_SELFHOST_STAGES=PASS'

printf 'hàm phụ() { trả về 53; };\n' > "$out/module.vi"
printf 'nhập module.*;\nhàm chính() { trả về phụ(); };\n' > "$out/multi.vi"
"$out/stage2" build "$out/multi.vi" -o "$out/multi"
set +e
"$out/multi"
status=$?
set -e
if [[ "$status" != 53 ]]; then
    echo "Direct ld multi-module exited $status, expected 53" >&2
    exit 1
fi
echo 'MACOS_DIRECT_LD_MULTIMODULE=PASS'

# The V++ Mach-O writer produces per-source ET_REL equivalents directly.
# Apple ld -r merges them without a Clang driver, SDK, or runtime ASM object.
# Setting the latter two inputs to empty is intentional: object-only links
# must not accidentally inherit executable-link requirements.
VPP_MACOS_SDKROOT= SDKROOT= VPP_MACOS_RUNTIME_OBJECT= \
    "$out/stage2" object "$out/multi.vi" -o "$out/multi-object.o"
file "$out/multi-object.o" | grep -q 'Mach-O 64-bit object arm64'
nm "$out/multi-object.o" > "$out/multi-object-symbols.log"
grep -q '_vpp_fn_' "$out/multi-object-symbols.log"
echo 'MACOS_DIRECT_LD_RELOCATABLE_MULTIMODULE=PASS'
VPP_MACOS_SDKROOT= SDKROOT= VPP_MACOS_RUNTIME_OBJECT= \
    "$out/stage2" object "$out/multi.vi" -o "$out/multi-object-repeat.o"
cmp "$out/multi-object.o" "$out/multi-object-repeat.o"
echo 'MACOS_DIRECT_LD_RELOCATABLE_DETERMINISM=PASS'

VPP_MACOS_SDKROOT= SDKROOT= VPP_MACOS_RUNTIME_OBJECT= \
    "$out/stage2" runtime-object "$out/minimal.vi" -o "$out/runtime-object.o"
file "$out/runtime-object.o" | grep -q 'Mach-O 64-bit object arm64'
nm "$out/runtime-object.o" > "$out/runtime-object-symbols.log"
grep -q '_vpp_fn_' "$out/runtime-object-symbols.log"
echo 'MACOS_DIRECT_LD_RELOCATABLE_RUNTIME=PASS'

# The V++ argument table passes UTF-8 paths directly to posix_spawn/ld;
# include both non-ASCII characters and spaces in the source and output.
mkdir -p "$out/đường dẫn có dấu cách"
cp "$out/minimal.vi" "$out/đường dẫn có dấu cách/thử nghiệm.vi"
"$out/stage2" build "$out/đường dẫn có dấu cách/thử nghiệm.vi" \
    -o "$out/đường dẫn có dấu cách/kết quả"
set +e
"$out/đường dẫn có dấu cách/kết quả"
status=$?
set -e
if [[ "$status" != 52 ]]; then
    echo "Direct ld Unicode/space path exited $status, expected 52" >&2
    exit 1
fi
echo 'MACOS_DIRECT_LD_UNICODE_PATH=PASS'

printf 'hàm chính() { trả về máy đặt quyền tệp mở(-1, 448); };\n' > "$out/fchmod.vi"
"$out/stage2" build "$out/fchmod.vi" -o "$out/fchmod"
set +e
"$out/fchmod"
status=$?
set -e
if [[ "$status" != 255 ]]; then
    echo "Direct ld fchmod(-1) exited $status, expected 255" >&2
    exit 1
fi
echo 'MACOS_DIRECT_LD_FCHMOD=PASS'

# A linker failure must not publish a truncated or missing former executable.
printf 'previous executable must survive\n' > "$out/preserved"
cp "$out/preserved" "$out/expected"
printf 'not a Mach-O object\n' > "$out/malformed.o"
if VPP_MACOS_RUNTIME_OBJECT="$out/malformed.o" \
    "$out/stage2" build "$out/minimal.vi" -o "$out/preserved" > "$out/link-failure.log" 2>&1; then
    echo 'Direct ld unexpectedly accepted malformed runtime object' >&2
    exit 1
fi
cmp "$out/preserved" "$out/expected"
if find "$out" -maxdepth 1 -type f \( -name '*.vpp-module-*' -o -name '*.vpp-exe-*' \) | grep -q .; then
    echo 'Direct ld left temporary output after failure' >&2
    exit 1
fi
echo 'MACOS_DIRECT_LD_ATOMIC_FAILURE=PASS'
