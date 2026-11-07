#!/usr/bin/env bash
# Windows x64 COFF ABI smoke under Wine on Linux/amd64 Docker.
# This complements, but does not replace, native Windows Actions testing.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
image="${VPP_WINDOWS_WINE_IMAGE:-vpp-windows-boundary-wine:ubuntu2404}"
work="$(mktemp -d "${TMPDIR:-/tmp}/vpp-windows-wine.XXXXXXXX")"
trap 'rm -rf "$work"' EXIT HUP INT TERM

"${VPP_CLANG:-clang}" -target x86_64-pc-windows-msvc \
    -DVPP_WINDOWS_BOUNDARY_TEST=1 -DVPP_NATIVE_RUNTIME_STACK_ABI=1 \
    -c "$root/scripts/ASM/windows/vpp_aot_primitives_windows_x86_64.S" \
    -o "$work/windows_file_boundary.obj"
"${VPP_CLANG:-clang}" -target x86_64-pc-windows-msvc \
    -DVPP_WINDOWS_STAGE0=1 -DVPP_NATIVE_RUNTIME_STACK_ABI=0 \
    -c "$root/scripts/ASM/windows/vpp_aot_primitives_windows_x86_64.S" \
    -o "$work/windows_stage0.obj"
cmake "-DVPP_ROOT=$root" "-DVPP_STAGE0_SYMBOLS=$work/stage0_symbols.c" \
    -P "$root/scripts/quality/windows_file_boundary/stage0_symbols.cmake"

docker build --platform linux/amd64 -f \
    "$root/scripts/quality/windows_file_boundary/Dockerfile.wine" \
    -t "$image" "$root/scripts/quality/windows_file_boundary"

docker run --rm --platform linux/amd64 --memory=2g --pids-limit=128 \
    -v "$root:/repo:ro" -v "$work:/work" \
    -e WINEDEBUG=-all -e LANG=C.UTF-8 -e LC_ALL=C.UTF-8 "$image" sh -ec '
        x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra \
            /repo/scripts/quality/windows_file_boundary/probe.c \
            /repo/scripts/quality/windows_file_boundary/mingw_crt_compat.c \
            /work/windows_file_boundary.obj -lbcrypt -lshell32 \
            -o /work/windows_file_boundary.exe
        WINEPREFIX=/tmp/vpp-wine-prefix timeout -k 10s 180s /usr/lib/wine/wine64 \
            /work/windows_file_boundary.exe "gói biên dịch có dấu 𠮷" ""
        x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra \
            /repo/scripts/quality/windows_file_boundary/stage0_probe.c \
            /repo/src/runtime/native/vpp_aot_runtime.c \
            /repo/scripts/quality/windows_file_boundary/mingw_crt_compat.c \
            /work/stage0_symbols.c /work/windows_stage0.obj -lbcrypt -lshell32 \
            -o /work/windows_stage0.exe
        cd /work
        VPP_HOME="Z:\\repo" WINEPREFIX=/tmp/vpp-wine-prefix \
            timeout -k 10s 180s /usr/lib/wine/wine64 \
            /work/windows_stage0.exe "gói biên dịch có dấu 𠮷" ""
    '
echo 'WINDOWS_DOCKER_WINE_BOUNDARY=PASS (Wine compatibility layer)'
