#!/usr/bin/env bash
# Cross-compile V++ i32 into AMD64 COFF using a self-hosted compiler, then
# verify object records and execute the linked programs via MinGW + Wine.
# Wine tests the Windows ABI; it does not certify a native Windows bootstrap.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ $# != 1 || ! -x "$1" ]]; then
    echo 'usage: check_windows_coff_selfhosting_docker.sh <self-hosted-compiler>' >&2
    exit 2
fi
compiler="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
image="${VPP_WINDOWS_WINE_IMAGE:-vpp-windows-boundary-wine:ubuntu2404}"
work="$(mktemp -d "${TMPDIR:-/tmp}/vpp-coff-selfhosting.XXXXXXXX")"
trap 'rm -rf "$work"' EXIT HUP INT TERM

if ! docker image inspect "$image" >/dev/null 2>&1; then
    docker build --platform linux/amd64 \
        -f "$root/scripts/quality/windows_file_boundary/Dockerfile.wine" \
        -t "$image" "$root/scripts/quality/windows_file_boundary"
fi

cat > "$work/minimal.vi" <<'EOF'
hàm chính() { trả về 52; };
EOF
cat > "$work/abi4.vi" <<'EOF'
hàm cộng(a,b,c,d) { trả về a + b*2 + c*3 + d*4; };
hàm chính() { trả về cộng(1,2,3,4); };
EOF
cat > "$work/abi8.vi" <<'EOF'
hàm cộng(a,b,c,d,e,f,g,h) { trả về a + b + c + d + e + f + g + h; };
hàm chính() { trả về cộng(1,2,3,4,5,6,7,8); };
EOF
cat > "$work/branch.vi" <<'EOF'
hàm chính() {
    biến x = 0;
    lặp (i = 0; i < 7; i++) {
        nếu (i < 3) { x = x + 2; } hoặc { x = x + 4; };
    };
    trả về x;
};
EOF
cat > "$work/runtime.vi" <<'EOF'
hàm chính() { trả về máy đặt quyền tệp mở(-1, 448); };
EOF
cat > "$work/stub.c" <<'EOF'
int vpp_native_os_fchmod(int descriptor, int mode) {
    return descriptor == -1 && mode == 448 ? 73 : 2;
}
EOF

for name in minimal abi4 abi8 branch runtime; do
    VPP_HOME="$root" VPP_NATIVE_NO_CACHE=1 VPP_NATIVE_TARGET=windows-x86_64 \
        "$compiler" object "$work/$name.vi" -o "$work/$name.obj"
done
python3 "$root/scripts/quality/check_windows_coff_objects.py" "$work"/*.obj

# A rejected output must preserve the user's source and leave no temporary file.
cp "$work/minimal.vi" "$work/minimal-original.vi"
if VPP_HOME="$root" VPP_NATIVE_NO_CACHE=1 VPP_NATIVE_TARGET=windows-x86_64 \
    "$compiler" object "$work/minimal.vi" -o "$work/minimal.vi" \
    > "$work/unsafe-output.log" 2>&1; then
    echo 'Windows COFF unexpectedly replaced its input source' >&2
    exit 1
fi
cmp "$work/minimal.vi" "$work/minimal-original.vi"

# COFF partial linking merges source modules into one relocatable object.
cat > "$work/module.vi" <<'EOF'
hàm phụ() { trả về 53; };
EOF
cat > "$work/multi.vi" <<'EOF'
nhập module.*;
hàm chính() { trả về phụ(); };
EOF
cat > "$work/helper.vi" <<'EOF'
hàm phụ_số() { trả về 6; };
EOF
cat > "$work/math.vi" <<'EOF'
hàm hàm_đặc_biệt_dài(a) { trả về a + phụ_số() + phụ_số(); };
EOF
cat > "$work/rt.vi" <<'EOF'
hàm phần_gọi_runtime() { trả về máy đặt quyền tệp mở(-1, 448); };
EOF
cat > "$work/complex.vi" <<'EOF'
nhập helper.*;
nhập math.*;
nhập rt.*;
hàm chính() { trả về hàm_đặc_biệt_dài(10) + hàm_đặc_biệt_dài(20) + phần_gọi_runtime(); };
EOF
for name in multi complex; do
    VPP_HOME="$root" VPP_NATIVE_NO_CACHE=1 VPP_NATIVE_TARGET=windows-x86_64 \
        "$compiler" object "$work/$name.vi" -o "$work/$name.obj"
done
python3 "$root/scripts/quality/check_windows_coff_objects.py" "$work/multi.obj" "$work/complex.obj"

# Failed source compilation must preserve an existing output atomically.
cat > "$work/bad.vi" <<'EOF'
hàm chính( { trả về 3; };
EOF
printf 'original file must survive\n' > "$work/bad.obj"
cp "$work/bad.obj" "$work/bad-original"
if VPP_HOME="$root" VPP_NATIVE_NO_CACHE=1 VPP_NATIVE_TARGET=windows-x86_64 \
    "$compiler" object "$work/bad.vi" -o "$work/bad.obj" \
    > "$work/bad.log" 2>&1; then
    echo 'Windows COFF accepted malformed V++ source' >&2
    exit 1
fi
cmp "$work/bad.obj" "$work/bad-original"
if find "$work" -maxdepth 1 -type f -name '*.vpp-module-*' | grep -q .; then
    echo 'Windows COFF left temporary object files after failure' >&2
    exit 1
fi

docker run --rm --platform linux/amd64 --memory=2g --pids-limit=128 \
    -v "$work:/work" -e WINEDEBUG=-all "$image" sh -ec '
    for name in minimal abi4 abi8 branch runtime multi complex; do
        case "$name" in
            runtime|complex) x86_64-w64-mingw32-gcc "/work/$name.obj" /work/stub.c -o "/work/$name.exe";;
            *) x86_64-w64-mingw32-gcc "/work/$name.obj" -o "/work/$name.exe";;
        esac
        case "$name" in
            minimal) expected=52;;
            abi4) expected=30;;
            abi8) expected=36;;
            branch) expected=22;;
            runtime) expected=73;;
            multi) expected=53;;
            complex) expected=127;;
        esac
        set +e
        WINEPREFIX=/tmp/vpp-coff-wine-prefix timeout -k 10s 90s \
            /usr/lib/wine/wine64 "/work/$name.exe"
        actual=$?
        set -e
        test "$actual" -eq "$expected" || {
            echo "Windows COFF $name returned $actual, expected $expected" >&2
            exit 1
        }
        echo "WINDOWS_COFF_WINE_${name}=PASS"
    done
    '
echo 'WINDOWS_COFF_SELFHOSTING_DOCKER=PASS (cross-compiled objects; Wine runtime)'
