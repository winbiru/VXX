#!/usr/bin/env bash
# Exercise native V++ MH_EXECUTE generation. Apple ld and SDK are intentionally
# unavailable to the freestanding executable path; codesign runs on the result.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ $# != 1 || ! -x "$1" || "$(uname -s)-$(uname -m)" != Darwin-arm64 ]]; then
    echo 'usage: check_macos_native_macho_executable.sh <native-vpp-compiler>' >&2
    exit 2
fi
compiler="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
work="$(mktemp -d "${TMPDIR:-/tmp}/vpp-macho-executable.XXXXXXXX")"
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/minimal.vi" <<'VI'
hàm chính() { trả về 52; };
VI
cat > "$work/calls.vi" <<'VI'
hàm phụ() { trả về 53; };
hàm chính() { trả về phụ(); };
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
cat > "$work/branch.vi" <<'VI'
hàm chính() { biến x=0; lặp (i=0; i<7; i++) { nếu (i<3) {x=x+2;} hoặc {x=x+4;}; }; trả về x; };
VI
cat > "$work/unresolved.vi" <<'VI'
hàm chính() { trả về máy đặt quyền tệp mở(-1, 448); };
VI
cat > "$work/bad.vi" <<'VI'
hàm chính( { trả về 3; };
VI

export VPP_HOME="$root" VPP_NATIVE_NO_CACHE=1 VPP_NATIVE_MACHO_EXE_LINK=1
export VPP_MACOS_DIRECT_LD=0 VPP_MACOS_SDKROOT= SDKROOT= VPP_MACOS_RUNTIME_OBJECT=
for name in minimal calls multi multi2 branch; do
    "$compiler" build "$work/$name.vi" -o "$work/$name"
    file "$work/$name" | grep -q 'Mach-O 64-bit executable arm64'
    codesign --verify "$work/$name"
    otool -l "$work/$name" | grep -q 'LC_MAIN'
    "$compiler" build "$work/$name.vi" -o "$work/$name-repeat"
    cmp "$work/$name" "$work/$name-repeat"
    set +e
    "$work/$name"
    actual=$?
    set -e
    case "$name" in
        minimal) expected=52;;
        calls|multi|multi2) expected=53;;
        branch) expected=22;;
    esac
    if [[ "$actual" != "$expected" ]]; then
        echo "Native Mach-O $name exit=$actual expected=$expected" >&2
        exit 1
    fi
    echo "MACOS_NATIVE_MACHO_${name}=PASS"
done

printf 'previous output remains intact\n' > "$work/preserved"
cp "$work/preserved" "$work/expected"
for name in unresolved bad; do
    if "$compiler" build "$work/$name.vi" -o "$work/preserved" > "$work/$name.log" 2>&1; then
        echo "Mach-O linker unexpectedly accepted $name" >&2
        exit 1
    fi
    cmp "$work/preserved" "$work/expected"
done
if find "$work" -type f \( -name '*.vpp-exe-*' -o -name '*.vpp-module-*' \) | grep -q .; then
    echo 'Mach-O linker leaked temporary files' >&2
    exit 1
fi
echo 'MACOS_NATIVE_MACHO_EXECUTABLE_GATE=PASS (freestanding V++; ad hoc signed)'
