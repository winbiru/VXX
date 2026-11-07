#!/usr/bin/env bash
# Bootstrap a V++ Windows AMD64 COFF compiler using a native PE seed and the
# V++ runtime object. Wine is a compatibility test, not a Windows CI result.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ $# != 2 || ! -f "$1" || ! -f "$2" ]]; then
    echo 'usage: check_windows_native_stage_convergence_docker.sh <stage1.exe> <vpp-runtime-coff.obj>' >&2
    exit 2
fi
image="${VPP_WINDOWS_WINE_IMAGE:-vpp-windows-boundary-wine:ubuntu2404}"
work="$(mktemp -d /tmp/vpp-windows-convergence.XXXXXXXX)"
trap 'rm -rf "$work"' EXIT HUP INT TERM

cp "$1" "$work/stage1.exe"
cp "$2" "$work/vpp-runtime.obj"
"${VPP_CLANG:-clang}" -target x86_64-pc-windows-msvc \
    -DVPP_NATIVE_RUNTIME_STACK_ABI=1 -c \
    "$root/scripts/ASM/windows/vpp_aot_primitives_windows_x86_64.S" \
    -o "$work/windows-runtime.obj"
printf 'hàm chính() { trả về 52; };\n' > "$work/gói thử nghiệm 𠮷.vi"

docker run --rm --platform linux/amd64 --memory=3g --pids-limit=128 \
    -v "$root:/repo:ro" -v "$work:/work" \
    -e VPP_HOME=Z:/repo -e VPP_NATIVE_NO_CACHE=1 \
    -e WINEDEBUG=-all -e LANG=C.UTF-8 -e LC_ALL=C.UTF-8 \
    "$image" sh -ec '
        export WINEPREFIX=/tmp/vpp-windows-convergence-wine
        compiler_source="Z:/repo/gói/biên dịch/bản địa/bộ biên dịch.vi"
        timeout -k 10s 300s /usr/lib/wine/wine64 /work/stage1.exe \
            object "$compiler_source" -o Z:/work/compiler-stage2.obj
        x86_64-w64-mingw32-gcc /work/compiler-stage2.obj \
            /work/vpp-runtime.obj /work/windows-runtime.obj \
            -lbcrypt -lshell32 -o /work/stage2.exe
        echo WINDOWS_STAGE2_PE=PASS

        timeout -k 10s 300s /usr/lib/wine/wine64 /work/stage2.exe \
            object "$compiler_source" -o Z:/work/compiler-stage3.obj
        cmp /work/compiler-stage2.obj /work/compiler-stage3.obj
        echo WINDOWS_COFF_STAGE2_STAGE3_EXACT_CMP=PASS

        x86_64-w64-mingw32-gcc /work/compiler-stage3.obj \
            /work/vpp-runtime.obj /work/windows-runtime.obj \
            -lbcrypt -lshell32 -o /work/stage3.exe
        timeout -k 10s 90s /usr/lib/wine/wine64 /work/stage3.exe \
            object "Z:/work/gói thử nghiệm 𠮷.vi" -o Z:/work/fixture.obj
        x86_64-w64-mingw32-gcc /work/fixture.obj -o /work/fixture.exe
        set +e
        timeout -k 10s 90s /usr/lib/wine/wine64 /work/fixture.exe
        actual=$?
        set -e
        test "$actual" -eq 52 || {
            echo "Windows Stage 3 fixture returned $actual, expected 52" >&2
            exit 1
        }
        echo WINDOWS_STAGE3_UNICODE_FIXTURE=PASS
        sha256sum /work/compiler-stage2.obj /work/compiler-stage3.obj
    '

python3 "$root/scripts/quality/check_windows_coff_objects.py" \
    "$work/compiler-stage2.obj" "$work/compiler-stage3.obj" "$work/fixture.obj"
echo 'WINDOWS_NATIVE_COFF_CONVERGENCE=PASS (Wine compatibility layer)'
