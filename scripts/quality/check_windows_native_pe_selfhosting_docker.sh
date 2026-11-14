#!/usr/bin/env bash
# Cross-create stage 1 using V++; stages 2/3 execute as PE under Wine. No
# MinGW, MSVC or external linker is used after preparing the ASM boundary.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ $# != 2 || ! -x "$1" || -e "$2" ]]; then
    echo 'usage: check_windows_native_pe_selfhosting_docker.sh <verified-compiler> <new-output-dir>' >&2
    exit 2
fi
seed="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
image="${VPP_WINDOWS_WINE_IMAGE:-vpp-windows-boundary-wine:ubuntu2404}"
docker image inspect "$image" >/dev/null
mkdir -p "$2"
out="$(cd "$2" && pwd)"
"${VPP_CLANG:-clang}" -target x86_64-pc-windows-msvc \
    -DVPP_NATIVE_RUNTIME_STACK_ABI=1 -c \
    "$root/scripts/ASM/windows/vpp_aot_primitives_windows_x86_64.S" \
    -o "$out/windows-runtime.obj"
VPP_HOME="$root" VPP_NATIVE_NO_CACHE=1 VPP_NATIVE_TARGET=windows-x86_64 \
    VPP_WINDOWS_RUNTIME_OBJECT="$out/windows-runtime.obj" \
    "$seed" build "$root/gói/biên dịch/bản địa/bộ biên dịch.vi" -o "$out/stage1.exe"
python3 "$root/scripts/quality/check_native_executable_layout.py" "$out/stage1.exe"
python3 "$root/scripts/quality/check_native_runtime_linker_inputs.py" pe \
    "$seed" "$out/windows-runtime.obj" "$out/input-regressions"
printf 'hàm chính() { trả về 52; };\n' > "$out/gói thử nghiệm 𠮷.vi"
# Compare signed i32 inside V++; use a portable positive process exit code.
printf 'hàm chính() { nếu (máy đặt quyền tệp mở(-1,448) == -1) { trả về 73; }; trả về 1; };\n' > "$out/runtime.vi"
printf 'old output must survive\n' > "$out/preserved.exe"
cp "$out/preserved.exe" "$out/expected"
printf 'bad COFF object\n' > "$out/malformed.obj"
docker run --rm --platform linux/amd64 --memory=3g --pids-limit=128 \
    -v "$root:/repo:ro" -v "$out:/work" -e WINEDEBUG=-all \
    -e LANG=C.UTF-8 -e LC_ALL=C.UTF-8 \
    -e VPP_HOME=Z:/repo -e VPP_NATIVE_NO_CACHE=1 \
    -e VPP_NATIVE_TARGET=windows-x86_64 \
    -e VPP_WINDOWS_RUNTIME_OBJECT=Z:/work/windows-runtime.obj \
    "$image" sh -ec '
    export WINEPREFIX=/tmp/vpp-native-pe-selfhost
    mkdir -p /work/forbid-tools
    export VPP_FORBIDDEN_TOOL_LOG=/work/forbidden-tools.log
    : > "$VPP_FORBIDDEN_TOOL_LOG"
    for name in cc gcc clang ld as x86_64-w64-mingw32-gcc x86_64-w64-mingw32-ld; do
        printf '\''#!/bin/sh\nprintf "%%s\\n" "$0 $*" >> "$VPP_FORBIDDEN_TOOL_LOG"\nexit 99\n'\'' > "/work/forbid-tools/$name"
        chmod +x "/work/forbid-tools/$name"
    done
    export PATH="/work/forbid-tools:$PATH"
    compiler=/work/stage1.exe
    for stage in 2 3; do
        timeout -k 10s "${VPP_SELFHOST_STAGE_TIMEOUT_SECONDS:-900}s" \
            /usr/lib/wine/wine64 "$compiler" build \
            "Z:/repo/gói/biên dịch/bản địa/bộ biên dịch.vi" \
            -o "Z:/work/stage$stage.exe" > "/work/stage$stage.log" 2>&1 || {
            cat "/work/stage$stage.log" >&2; exit 1;
        }
        compiler="/work/stage$stage.exe"
        echo "WINDOWS_NATIVE_PE_STAGE${stage}=PASS"
    done
    cmp /work/stage1.exe /work/stage2.exe
    cmp /work/stage2.exe /work/stage3.exe
    sha256sum /work/stage1.exe /work/stage2.exe /work/stage3.exe
    echo WINDOWS_NATIVE_PE_SELFHOST_EXACT_CMP=PASS
    for name in "gói thử nghiệm 𠮷" runtime; do
        timeout -k 10s 120s /usr/lib/wine/wine64 "$compiler" build \
            "Z:/work/$name.vi" -o "Z:/work/$name.exe"
        set +e
        timeout -k 10s 60s /usr/lib/wine/wine64 "/work/$name.exe"
        actual=$?
        set -e
        expected=52
        test "$name" != runtime || expected=73
        test "$actual" -eq "$expected" || {
            echo "PE probe $name exit=$actual expected=$expected" >&2; exit 1;
        }
    done
    set +e
    VPP_WINDOWS_RUNTIME_OBJECT=Z:/work/malformed.obj \
        timeout -k 10s 120s /usr/lib/wine/wine64 "$compiler" build \
        Z:/work/runtime.vi -o Z:/work/preserved.exe > /work/rejected.log 2>&1
    rejected_exit=$?
    set -e
    test "$rejected_exit" -eq 17 || {
        echo "PE malformed boundary returned $rejected_exit (expected 17)" >&2
        cat /work/rejected.log >&2
        exit 1
    }
    cmp /work/preserved.exe /work/expected
    echo WINDOWS_NATIVE_PE_ATOMIC_FAILURE=PASS
    test ! -s "$VPP_FORBIDDEN_TOOL_LOG"
    echo WINDOWS_NATIVE_PE_EXTERNAL_BUILD_TOOLS=NONE
    '
python3 "$root/scripts/quality/check_native_executable_layout.py" \
    "$out/stage2.exe" "$out/stage3.exe"
if find "$out" -type f \( -name '*.vpp-exe-*' -o -name '*.vpp-module-*' \) | grep -q .; then
    echo 'Native PE linker left temporary files' >&2; exit 1
fi
echo 'WINDOWS_NATIVE_PE_SELFHOST=PASS (Wine in Docker; native Windows execution pending)'
