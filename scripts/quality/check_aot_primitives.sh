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

# The same assembler workflow also validates the direct V++ file syscall ABI.
for target in arm64-apple-darwin x86_64-apple-darwin aarch64-unknown-linux-gnu x86_64-unknown-linux-gnu; do
    "$compiler" -target "$target" -c "$repo_root/scripts/ASM/vpp_file_primitives.S" \
        -o "$work/file-${target}.o"
    test -s "$work/file-${target}.o"
    printf 'PASS file machine %s\n' "$target"
done
# POSIX timezone state is a process-library contract; verify the ABI bridge
# is assemblable on every supported Unix target.
for target in arm64-apple-darwin x86_64-apple-darwin aarch64-unknown-linux-gnu x86_64-unknown-linux-gnu; do
    "$compiler" -target "$target" -c "$repo_root/scripts/ASM/vpp_localtime_primitives.S" \
        -o "$work/localtime-${target}.o"
    test -s "$work/localtime-${target}.o"
    printf 'PASS localtime machine %s\n' "$target"
done

# Darwin directory stream functions branch to libc; Linux uses direct
# openat/getdents64/close syscalls through vpp_file_primitives.S.
for target in arm64-apple-darwin x86_64-apple-darwin aarch64-unknown-linux-gnu x86_64-unknown-linux-gnu; do
    "$compiler" -target "$target" -c "$repo_root/scripts/ASM/vpp_directory_primitives.S" \
        -o "$work/directory-${target}.o"
    test -s "$work/directory-${target}.o"
    printf 'PASS directory machine %s\n' "$target"
done

for target in arm64-apple-darwin x86_64-apple-darwin aarch64-unknown-linux-gnu x86_64-unknown-linux-gnu; do
    "$compiler" -target "$target" -c "$repo_root/scripts/ASM/vpp_path_primitives.S" \
        -o "$work/path-${target}.o"
    test -s "$work/path-${target}.o"
    printf 'PASS path machine %s\n' "$target"
done

# Linux path and directory objects must never regress to imports of libc
# stat/lstat/opendir/readdir/closedir. Check undefined symbols when llvm-nm
# is installed; cross-assembly remains usable without optional LLVM tools.
llvm_nm=
if command -v llvm-nm >/dev/null 2>&1; then
    llvm_nm=$(command -v llvm-nm)
elif command -v xcrun >/dev/null 2>&1; then
    llvm_nm=$(xcrun --find llvm-nm 2>/dev/null || true)
fi
if test -n "$llvm_nm"; then
    for target in aarch64-unknown-linux-gnu x86_64-unknown-linux-gnu; do
        for source in path directory; do
            unresolved=$("$llvm_nm" --undefined-only "$work/${source}-${target}.o")
            test -z "$unresolved" || {
                printf 'FAIL Linux %s %s has undefined native imports:\n%s\n' \
                    "$source" "$target" "$unresolved" >&2
                exit 1
            }
        done
    done
    printf 'PASS Linux path/directory ASM without external imports\n'
fi

# Darwin clock_gettime uses a public libSystem API at an ASM-only boundary.
# Verify both CPU ABIs: the x86 tail call and arm64 branch have no C shim.
for target in arm64-apple-darwin x86_64-apple-darwin; do
    "$compiler" -target "$target" -c "$repo_root/scripts/ASM/vpp_clock_primitives.S" \
        -o "$work/clock-${target}.o"
    test -s "$work/clock-${target}.o"
    printf 'PASS clock machine %s\n' "$target"
done

for target in arm64-apple-darwin x86_64-apple-darwin; do
    "$compiler" -target "$target" -c "$repo_root/scripts/ASM/vpp_entropy_primitives.S" \
        -o "$work/entropy-${target}.o"
    test -s "$work/entropy-${target}.o"
    printf 'PASS entropy machine %s\n' "$target"
done

for target in arm64-apple-darwin x86_64-apple-darwin aarch64-unknown-linux-gnu x86_64-unknown-linux-gnu; do
    "$compiler" -target "$target" -c "$repo_root/scripts/ASM/vpp_environment_primitives.S" \
        -o "$work/env-${target}.o"
    test -s "$work/env-${target}.o"
    printf 'PASS environment machine %s\n' "$target"
done
