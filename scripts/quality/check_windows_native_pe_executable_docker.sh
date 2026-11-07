#!/usr/bin/env bash
# Validate the V++ PE32+ executable linker with real Wine execution.
# This is a cross-host executable gate; it does not assert native Windows bootstrap.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ $# != 1 || ! -x "$1" ]]; then
    echo 'usage: check_windows_native_pe_executable_docker.sh <native-vpp-compiler>' >&2
    exit 2
fi
compiler="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
image="${VPP_WINDOWS_WINE_IMAGE:-vpp-windows-boundary-wine:ubuntu2404}"
work="$(mktemp -d "${TMPDIR:-/tmp}/vpp-pe-executable.XXXXXXXX")"
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/minimal.vi" <<'VI'
hàm chính() { trả về 52; };
VI
cat > "$work/calls.vi" <<'VI'
hàm phụ() { trả về 53; };
hàm chính() { trả về phụ(); };
VI
cat > "$work/abi4.vi" <<'VI'
hàm cộng(a,b,c,d) { trả về a + b*2 + c*3 + d*4; };
hàm chính() { trả về cộng(1,2,3,4); };
VI
cat > "$work/abi8.vi" <<'VI'
hàm cộng(a,b,c,d,e,f,g,h) { trả về a + b + c + d + e + f + g + h; };
hàm chính() { trả về cộng(1,2,3,4,5,6,7,8); };
VI
cat > "$work/branch.vi" <<'VI'
hàm chính() { biến x=0; lặp (i=0; i<7; i++) { nếu (i<3) {x=x+2;} hoặc {x=x+4;}; }; trả về x; };
VI
cat > "$work/helper.vi" <<'VI'
hàm phụ() { trả về 53; };
VI
cat > "$work/multi.vi" <<'VI'
nhập helper.*;
hàm chính() { trả về phụ(); };
VI
cat > "$work/helper2.vi" <<'VI'
hàm thêm(a) { trả về a + 7; };
VI
cat > "$work/multi2.vi" <<'VI'
nhập helper2.*;
hàm chính() { trả về thêm(thêm(39)); };
VI
cat > "$work/unresolved.vi" <<'VI'
hàm chính() { trả về máy đặt quyền tệp mở(-1, 448); };
VI
cat > "$work/bad.vi" <<'VI'
hàm chính( { trả về 3; };
VI

for name in minimal calls abi4 abi8 branch multi multi2; do
    VPP_HOME="$root" VPP_NATIVE_NO_CACHE=1 VPP_NATIVE_TARGET=windows-x86_64 \
        "$compiler" build "$work/$name.vi" -o "$work/$name.exe"
    file "$work/$name.exe" | grep -q 'PE32+ executable (console) x86-64'
    VPP_HOME="$root" VPP_NATIVE_NO_CACHE=1 VPP_NATIVE_TARGET=windows-x86_64 \
        "$compiler" build "$work/$name.vi" -o "$work/$name-repeat.exe"
    cmp "$work/$name.exe" "$work/$name-repeat.exe"
done

printf 'previous output remains intact\n' > "$work/preserved.exe"
cp "$work/preserved.exe" "$work/expected"
for name in unresolved bad; do
    if VPP_HOME="$root" VPP_NATIVE_NO_CACHE=1 VPP_NATIVE_TARGET=windows-x86_64 \
        "$compiler" build "$work/$name.vi" -o "$work/preserved.exe" > "$work/$name.log" 2>&1; then
        echo "PE linker unexpectedly accepted $name" >&2
        exit 1
    fi
    cmp "$work/preserved.exe" "$work/expected"
done
if find "$work" -type f \( -name '*.vpp-exe-*' -o -name '*.vpp-module-*' \) | grep -q .; then
    echo 'PE linker leaked temporary files' >&2
    exit 1
fi

docker image inspect "$image" >/dev/null
docker run --rm --platform linux/amd64 --memory=2g --pids-limit=128 \
    -v "$work:/work" -e WINEDEBUG=-all "$image" sh -ec '
    for name in minimal calls abi4 abi8 branch multi multi2; do
        case "$name" in
            minimal) expected=52;;
            calls|multi) expected=53;;
            abi4) expected=30;;
            abi8) expected=36;;
            branch) expected=22;;
            multi2) expected=53;;
        esac
        set +e
        WINEPREFIX=/tmp/vpp-pe-wine-prefix timeout -k 10s 90s \
            /usr/lib/wine/wine64 "/work/$name.exe"
        actual=$?
        set -e
        test "$actual" -eq "$expected" || {
            echo "PE $name exit=$actual, expected=$expected" >&2
            exit 1
        }
        echo "WINDOWS_NATIVE_PE_${name}=PASS"
    done
    '
echo 'WINDOWS_NATIVE_PE_EXECUTABLE_GATE=PASS (Wine execution; no Windows compiler self-host claim)'
