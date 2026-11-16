#!/usr/bin/env bash
# Dedicated worker-enabled, freestanding VM; no AOT program metadata required.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
compiler=${1:?i32 compiler required}
output=${2:?VM output required}
[[ $(uname -s)-$(uname -m) == Linux-x86_64 ]] || exit 2
work=$(mktemp -d "${TMPDIR:-/tmp}/vpp-linux-vm.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
export VPP_HOME="$root" VPP_NATIVE_NO_CACHE=1 VPP_NATIVE_ELF_LINK=1
"$compiler" runtime-object "$root/gói/máy ảo/chính.vi" -o "$work/vm.o"
clang_bin=${VPP_AOT_ASSEMBLER:-clang}
objects=("$work/vm.o")
for part in start syscalls runtime_core directory sha256 process socket aot_runtime_bridge f64; do
    "$clang_bin" -target x86_64-unknown-linux-gnu -DVPP_STANDALONE_VM_WORKER=1 \
        -DVPP_JIT_DISABLED=1 -I "$root/scripts/ASM" -I "$root/scripts/ASM/common" \
        -c "$root/scripts/ASM/linux/vpp_linux_${part}_x86_64.S" -o "$work/$part.o"
    objects+=("$work/$part.o")
done
"$clang_bin" -nostdlib -static -no-pie -Wl,-e,_start -Wl,--no-undefined \
    -Wl,--build-id=none -Wl,-z,noexecstack "${objects[@]}" -o "$output"
bash "$root/scripts/quality/audit_linux_static_elf.sh" "$output"
