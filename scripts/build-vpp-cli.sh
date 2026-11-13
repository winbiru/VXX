#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out=${1:-"$root/build-pure-vpp"}
full=${2:-"$root/bootstrap/macos-arm64/vpp-native"}
i32=${3:-"$root/bootstrap/macos-arm64/vpp-i32"}
if [[ $(uname -s)-$(uname -m) != Darwin-arm64 ]]; then
  echo 'V++: full autonomous bootstrap currently requires macOS ARM64.' >&2
  exit 2
fi
(cd "$root/bootstrap/macos-arm64" && shasum -a 256 -c SHA256SUMS)
audit() {
  local seed=$1 libs symbols
  test -x "$seed"
  file "$seed" | grep -q 'Mach-O 64-bit executable arm64'
  /usr/bin/codesign --verify "$seed"
  libs=$(/usr/bin/otool -L "$seed" | tail -n +2 | awk '{print $1}')
  test "$libs" = /usr/lib/libSystem.B.dylib
  symbols=$(/usr/bin/nm -g "$seed")
  if grep -Eq '__cxa_|__Z[0-9NSt]|ffi_|vm_step|linkedBytecodeRun|executeVpp|bytecode_dispatch' <<< "$symbols"; then
    echo "V++: legacy host symbols in $seed" >&2; return 1
  fi
}
audit "$full"
audit "$i32"
mkdir -p "$out/bin"
out=$(cd "$out" && pwd)
export VPP_HOME="$root" VPP_NATIVE_NO_CACHE=1
runtime="$out/native-vpp-runtime"
bootstrap_runtime="$out/bootstrap-runtime"
mkdir -p "$runtime"
# Compile the i32 compiler and runtime from current V++ sources.
VPP_NATIVE_MACHO_REL_LINK=0 bash "$root/scripts/build-vpp-aot-runtime.sh" "$bootstrap_runtime" "$i32"
audit "$bootstrap_runtime/vpp-i32"
export VPP_AOT_VPP_OBJECT="$bootstrap_runtime/runtime.o" VPP_AOT_PLATFORM_OBJECT="$bootstrap_runtime/platform.o" VPP_AOT_DYNAMIC_BRIDGE_OBJECT="$bootstrap_runtime/dynamic-bridge.o"
# A previous-generation seed can emit ASM rather than direct Mach-O.
# Permit the system assembler/linker for this bootstrap step only.
export VPP_AOT_NATIVE_MACHO_LINK=0 VPP_AOT_NATIVE_ARM64_OBJECT=0
export VPP_AOT_ASSEMBLER=/usr/bin/clang
# Publish only a complete and audited full-language compiler.
"$full" build "$root/gói/biên dịch/dòng lệnh AOT tự chủ.vi" -o "$out/bin/vpp-native.new"
audit "$out/bin/vpp-native.new"
# Apple ld -r adds N_NO_DEAD_STRIP to the bootstrap runtime.
# The direct V++ linker needs the canonical V++ object.
VPP_NATIVE_MACHO_REL_LINK=1 "$bootstrap_runtime/vpp-i32" runtime-object \
  "$root/gói/máy ảo/runtime AOT.vi" -o "$runtime/runtime-direct.o"
test -s "$runtime/runtime-direct.o"
file "$runtime/runtime-direct.o" | grep -q 'Mach-O 64-bit object arm64'
cp "$bootstrap_runtime/platform.o" "$bootstrap_runtime/dynamic-bridge.o" "$runtime/"
mv -f "$runtime/runtime-direct.o" "$runtime/runtime.o"
mv -f "$out/bin/vpp-native.new" "$out/bin/vpp-native"
cp "$bootstrap_runtime/vpp-i32" "$out/bin/vpp-i32"
quoted_root=$(printf '%q' "$root")
escaped_root=$(printf '%s' "$quoted_root" | sed 's/[\\&|]/\\&/g')
sed "s|@VPP_SOURCE_ROOT@|$escaped_root|g" "$root/scripts/vpp-native-launcher.sh.in" > "$out/bin/vpp"
chmod 755 "$out/bin/vpp"
