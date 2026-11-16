#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
compiler=${1:?i32 compiler required}
output=${2:?VM output required}
# Link a dedicated platform object so ordinary AOT/raw executables do not
# acquire a reference to the interpreter callback or a registration service.
work=$(mktemp -d "${TMPDIR:-/tmp}/vpp-vm-build.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
export VPP_HOME="$root" VPP_NATIVE_NO_CACHE=1
"$compiler" runtime-object "$root/gói/máy ảo/chính.vi" -o "$work/vm.o"
/usr/bin/clang -arch arm64 -DVPP_NATIVE_RUNTIME_STACK_ABI=1 -DVPP_STANDALONE_VM_WORKER=1 \
  -c "$root/scripts/ASM/macos/vpp_aot_primitives_aarch64.S" -o "$work/platform.o"
/usr/bin/clang -arch arm64 "$work/vm.o" "$work/platform.o" -o "$output"
