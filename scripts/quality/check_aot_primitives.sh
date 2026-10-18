#!/bin/sh
# Cross-assemble the V++ platform boundary for both public ABIs.
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
compiler=${VPP_CLANG:-clang}
work=$(mktemp -d "${TMPDIR:-/tmp}/vpp-asm.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

while IFS='|' read -r target source; do
    for abi in 0 1; do
        "$compiler" -target "$target" -DVPP_NATIVE_RUNTIME_STACK_ABI="$abi" \
            -c "$repo_root/scripts/ASM/$source" \
            -o "$work/${source}.abi${abi}.o"
        test -s "$work/${source}.abi${abi}.o"
        printf 'PASS %s ABI=%s\n' "$target" "$abi"
    done
done <<'TARGETS'
arm64-apple-darwin|vpp_aot_primitives_aarch64.S
x86_64-apple-darwin|vpp_aot_primitives_macos_x86_64.S
aarch64-unknown-linux-gnu|vpp_aot_primitives_linux_aarch64.S
x86_64-unknown-linux-gnu|vpp_aot_primitives_linux_x86_64.S
x86_64-pc-windows-msvc|vpp_aot_primitives_windows_x86_64.S
TARGETS
