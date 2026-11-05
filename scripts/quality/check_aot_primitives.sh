#!/bin/sh
# Cross-assemble the V++ platform boundary for both public ABIs.
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
compiler=${VPP_CLANG:-clang}
work=$(mktemp -d "${TMPDIR:-/tmp}/vpp-asm.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

# Generated AOT source from existing self-hosted seeds uses the flat include
# name; current Stage 0 emits common/name after the ASM directory move.
# Both must compile with the include flags passed by native_aot_link/CMakeLists.
for target in arm64-apple-darwin x86_64-unknown-linux-gnu x86_64-pc-windows-msvc; do
    for layout in legacy organized; do
        if test "$layout" = legacy; then
            runtime_include='vpp_aot_dynamic_runtime.inc'
        else
            runtime_include='common/vpp_aot_dynamic_runtime.inc'
        fi
        printf '#define VPP_NATIVE_DYNAMIC_RUNTIME 1\n#include "%s"\n' \
            "$runtime_include" > "$work/aot-include.S"
        "$compiler" -target "$target" -x assembler-with-cpp \
            -I"$repo_root/scripts/ASM" -I"$repo_root/scripts/ASM/common" \
            -c "$work/aot-include.S" -o "$work/aot-${layout}-${target}.o"
        test -s "$work/aot-${layout}-${target}.o"
        printf 'PASS AOT %s include=%s\n' "$target" "$layout"
    done
done

while IFS='|' read -r target source; do
    for abi in 0 1; do
        "$compiler" -target "$target" -DVPP_NATIVE_RUNTIME_STACK_ABI="$abi" \
            -c "$repo_root/scripts/ASM/$source" \
            -o "$work/${source##*/}.abi${abi}.o"
        test -s "$work/${source##*/}.abi${abi}.o"
        printf 'PASS %s ABI=%s\n' "$target" "$abi"
    done
done <<'TARGETS'
arm64-apple-darwin|macos/vpp_aot_primitives_aarch64.S
x86_64-apple-darwin|macos/vpp_aot_primitives_macos_x86_64.S
aarch64-unknown-linux-gnu|linux/vpp_aot_primitives_linux_aarch64.S
x86_64-unknown-linux-gnu|linux/vpp_aot_primitives_linux_x86_64.S
x86_64-pc-windows-msvc|windows/vpp_aot_primitives_windows_x86_64.S
TARGETS

# Stage 0 links the C bootstrap with machine primitives on both Linux ABIs.
# Compile the isolated objects and inspect exports without a Linux host linker.
if command -v llvm-nm >/dev/null 2>&1; then
    linux_stage0_nm=$(command -v llvm-nm)
elif command -v xcrun >/dev/null 2>&1; then
    linux_stage0_nm=$(xcrun --find llvm-nm 2>/dev/null || true)
else
    linux_stage0_nm=$(command -v nm || true)
fi
if test -z "$linux_stage0_nm"; then
    echo 'FAIL Linux Stage 0 symbol inspection tool missing' >&2
    exit 1
fi
for arch in x86_64 aarch64; do
    "$compiler" -target "$arch-unknown-linux-gnu" -c \
        "$repo_root/scripts/ASM/linux/vpp_aot_stage0_machine_${arch}.S" \
        -o "$work/linux-stage0-${arch}.o"
    test -s "$work/linux-stage0-${arch}.o"
    "$linux_stage0_nm" --defined-only "$work/linux-stage0-${arch}.o" \
        > "$work/linux-stage0-${arch}-syms"
    for symbol in vpp_native_memory_bind vpp_native_memory_read_byte \
        vpp_native_memory_write_byte vpp_native_memory_read_i32 \
        vpp_native_memory_write_i32 vpp_native_i32_div \
        vpp_native_os_read vpp_native_os_write vpp_native_os_open \
        vpp_native_os_close vpp_native_os_errno vpp_native_os_mkstemp \
        vpp_native_os_rename vpp_native_os_fchmod vpp_native_os_fstat \
        vpp_native_os_lstat vpp_native_os_stat vpp_native_os_access_exec \
        vpp_native_os_unlink vpp_native_os_mkdir vpp_native_os_spawn_clang \
        vpp_native_os_waitpid vpp_native_os_env_copy \
        vpp_native_os_executable_path vpp_native_os_home vpp_native_os_cwd \
        vpp_native_sha_begin vpp_native_sha_update vpp_native_sha_end \
        vpp_native_os_register_cache_report; do
        if ! grep -Eq "[[:space:]][TW][[:space:]]${symbol}\$" \
            "$work/linux-stage0-${arch}-syms"; then
            printf 'FAIL Linux %s Stage 0 missing %s\n' "$arch" "$symbol" >&2
            exit 1
        fi
    done
    for symbol in vpp_native_argc vpp_native_os_argument_length \
        vpp_native_bytes_new vpp_native_file_open_read; do
        if grep -Eq "[[:space:]][TW][[:space:]]${symbol}\$" \
            "$work/linux-stage0-${arch}-syms"; then
            printf 'FAIL Linux %s Stage 0 duplicates C bootstrap %s\n' \
                "$arch" "$symbol" >&2
            exit 1
        fi
    done
    printf 'PASS Linux %s Stage 0 machine primitives\n' "$arch"
done

# Nguồn Linux tự chủ phải cho cùng đối tượng khi ghép văn bản hoặc dùng CPP.
# Đường V++ chỉ dùng cách ghép văn bản; CPP ở đây là đối chứng riêng.
VPP_LINUX_HOST_COMPILER= VPP_LINUX_HOST_ASSEMBLER="$compiler" \
    bash "$repo_root/scripts/quality/prepare_linux_freestanding_host.sh" \
    "$work/linux-host-flat.o"
"$compiler" -target x86_64-unknown-linux-gnu -x assembler-with-cpp \
    -c "$repo_root/scripts/ASM/linux/vpp_linux_host_x86_64.s" \
    -o "$work/linux-host-cpp.o"
cmp "$work/linux-host-flat.o" "$work/linux-host-cpp.o"
printf 'PASS Linux: ghép nguồn chức năng khớp CPP từng byte\n'

# Validate that Windows file publication can replace an existing executable
# and that new parser primitive 78 has a fail-closed COFF ABI entrypoint.
# Cross-assembly alone does not establish that a Windows executable runs.
if command -v llvm-objdump >/dev/null 2>&1; then
    windows_objdump=$(command -v llvm-objdump)
elif command -v xcrun >/dev/null 2>&1; then
    windows_objdump=$(xcrun --find llvm-objdump 2>/dev/null || true)
else
    windows_objdump=
fi
if test -n "$windows_objdump"; then
    "$windows_objdump" --syms "$work/vpp_aot_primitives_windows_x86_64.S.abi0.o" > "$work/windows-syms"
    grep -Eq '[[:space:]]MoveFileExW$' "$work/windows-syms"
    grep -Eq '[[:space:]]vpp_native_os_fchmod$' "$work/windows-syms"
    for symbol in vpp_native_os_mkstemp vpp_native_os_stat \
        vpp_native_os_lstat vpp_native_os_fstat _wmktemp_s _get_osfhandle \
        CreateFileW GetFileInformationByHandle _wspawnvp _cwait \
        _wopen _wunlink _wmkdir _waccess GetCommandLineW \
        CommandLineToArgvW MultiByteToWideChar WideCharToMultiByte \
        GetEnvironmentVariableW SetLastError GetLastError \
        BCryptOpenAlgorithmProvider BCryptCreateHash BCryptHashData \
        BCryptFinishHash BCryptDestroyHash BCryptCloseAlgorithmProvider; do
        grep -Eq "[[:space:]]${symbol}\$" "$work/windows-syms" || {
            printf 'FAIL Windows boundary missing %s\n' "$symbol" >&2
            exit 1
        }
    done
    if grep -Eq '[[:space:]]rename$' "$work/windows-syms"; then
        echo 'FAIL Windows runtime still imports CRT rename' >&2
        exit 1
    fi
    echo 'PASS Windows COFF file identity, exclusive temp creation and publication symbols'
fi

# Standalone VM machine helpers: validate their pointer conversion and OS
# calling conventions independently from the larger platform object.
for target in arm64-apple-darwin x86_64-apple-darwin aarch64-unknown-linux-gnu x86_64-unknown-linux-gnu; do
    for kind in system directory process socket; do
        "$compiler" -target "$target" -x assembler-with-cpp \
            -c "$repo_root/scripts/ASM/common/posix/vpp_vm_${kind}_primitives.inc" \
            -o "$work/vm-${kind}-${target}.o"
        test -s "$work/vm-${kind}-${target}.o"
        printf 'PASS VM %s machine %s\n' "$kind" "$target"
    done
done

# The same assembler workflow also validates the direct V++ file syscall ABI.
for target in arm64-apple-darwin x86_64-apple-darwin aarch64-unknown-linux-gnu x86_64-unknown-linux-gnu; do
    "$compiler" -target "$target" -c "$repo_root/scripts/ASM/common/posix/vpp_file_primitives.S" \
        -o "$work/file-${target}.o"
    test -s "$work/file-${target}.o"
    printf 'PASS file machine %s\n' "$target"
done
# POSIX timezone state is a process-library contract; verify the ABI bridge
# is assemblable on every supported Unix target.
for target in arm64-apple-darwin x86_64-apple-darwin aarch64-unknown-linux-gnu x86_64-unknown-linux-gnu; do
    "$compiler" -target "$target" -c "$repo_root/scripts/ASM/common/posix/vpp_localtime_primitives.S" \
        -o "$work/localtime-${target}.o"
    test -s "$work/localtime-${target}.o"
    printf 'PASS localtime machine %s\n' "$target"
done

# Darwin directory stream functions branch to libc; Linux uses direct
# openat/getdents64/close syscalls through common/posix/vpp_file_primitives.S.
for target in arm64-apple-darwin x86_64-apple-darwin aarch64-unknown-linux-gnu x86_64-unknown-linux-gnu; do
    "$compiler" -target "$target" -c "$repo_root/scripts/ASM/common/posix/vpp_directory_primitives.S" \
        -o "$work/directory-${target}.o"
    test -s "$work/directory-${target}.o"
    printf 'PASS directory machine %s\n' "$target"
done

for target in arm64-apple-darwin x86_64-apple-darwin aarch64-unknown-linux-gnu x86_64-unknown-linux-gnu; do
    "$compiler" -target "$target" -c "$repo_root/scripts/ASM/common/posix/vpp_path_primitives.S" \
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
        for kind in system directory; do
            unresolved=$("$llvm_nm" --undefined-only "$work/vm-${kind}-${target}.o")
            if printf '%s\n' "$unresolved" | grep -Eq 'clock_gettime|getentropy|getrandom|nanosleep|poll|errno|opendir|readdir|closedir|memcpy|memchr|rmdir|mkdir|_Z'; then
                printf 'FAIL Linux VM %s ASM has external OS/C++ imports:\n%s\n' "$kind" "$unresolved" >&2
                exit 1
            fi
        done
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
    "$compiler" -target "$target" -c "$repo_root/scripts/ASM/macos/vpp_clock_primitives.S" \
        -o "$work/clock-${target}.o"
    test -s "$work/clock-${target}.o"
    printf 'PASS clock machine %s\n' "$target"
done

for target in arm64-apple-darwin x86_64-apple-darwin; do
    "$compiler" -target "$target" -c "$repo_root/scripts/ASM/macos/vpp_entropy_primitives.S" \
        -o "$work/entropy-${target}.o"
    test -s "$work/entropy-${target}.o"
    printf 'PASS entropy machine %s\n' "$target"
done

for target in arm64-apple-darwin x86_64-apple-darwin aarch64-unknown-linux-gnu x86_64-unknown-linux-gnu; do
    "$compiler" -target "$target" -c "$repo_root/scripts/ASM/common/posix/vpp_environment_primitives.S" \
        -o "$work/env-${target}.o"
    test -s "$work/env-${target}.o"
    printf 'PASS environment machine %s\n' "$target"
done
