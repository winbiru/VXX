#!/usr/bin/env bash
# Bootstrap macOS ARM64 native i32 V++ from a pre-fchmod seed, without touching
# the source checkout. An isolated Stage 5 bridges the newly added primitive.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ $# != 2 || "$(uname -s)-$(uname -m)" != Darwin-arm64 ]]; then
    echo 'usage: check_macos_native_selfhosting.sh <existing-native-seed> <new-output-directory>' >&2
    exit 2
fi
seed="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
if [[ ! -x "$seed" || -e "$2" ]]; then
    echo 'Seed must be executable and output directory must not exist.' >&2
    exit 2
fi
mkdir -p "$2"
out="$(cd "$2" && pwd)"
shim="$out/legacy-seed-shim"
native="$shim/gói/biên dịch/bản địa"
asm="$shim/scripts/ASM"
mkdir -p "$native" "$asm"

# The old seed knows primitives 0..77 and the old flat ASM path. Supply a
# transitional V++ runtime with fchmod only stubbed while compiling Stage 5.
# Stage 6+ must compile against *all* unchanged source in the actual checkout.
python3 - "$root" "$native" "$asm" <<'PY'
from pathlib import Path
import sys

root, native, asm = map(Path, sys.argv[1:])
src = root / 'gói/biên dịch/bản địa'
for path in src.glob('*.vi'):
    (native / path.name).write_bytes(path.read_bytes())
linker = (src / 'trình liên kết elf.vi').read_text()
old = 'trả về máy đặt quyền tệp mở(mã tệp mở, 448); // 0700'
if linker.count(old) != 1:
    raise SystemExit('bootstrap bridge needs updating: ELF fchmod call changed')
(native / 'trình liên kết elf.vi').write_text(linker.replace(
    old, 'trả về 0; // bootstrap seed only; restored by Stage 6'))
publication = (src / 'trình xuất đối tượng.vi').read_text()
old = 'máy đặt quyền tệp mở(fd, 448)'
if publication.count(old) != 1:
    raise SystemExit('bootstrap bridge needs updating: Mach-O fchmod call changed')
(native / 'trình xuất đối tượng.vi').write_text(publication.replace(old, '0'))
platform = root / 'scripts/ASM'
for directory in ('macos', 'linux', 'windows', 'common'):
    (asm / directory).symlink_to(platform / directory, target_is_directory=True)
# Seed cũ dùng đường ASM phẳng. Chèn điểm vào mới để mọi phần chức năng
# tiếp tục tìm đường tương đối từ đúng thư mục macos/.
(asm / 'vpp_aot_primitives_aarch64.S').write_text(
    '#include "macos/vpp_aot_primitives_aarch64.S"\n')
PY

source="$root/gói/biên dịch/bản địa/bộ biên dịch.vi"
probe="$out/minimal.vi"
printf 'hàm chính() { trả về 52; };\n' > "$probe"

VPP_HOME="$shim" "$seed" build "$native/bộ biên dịch.vi" -o "$out/stage5-bridge"
VPP_HOME="$root" "$out/stage5-bridge" check "$probe"
VPP_HOME="$root" "$out/stage5-bridge" build "$source" -o "$out/stage6"
VPP_HOME="$root" "$out/stage6" check "$probe"
VPP_HOME="$root" "$out/stage6" build "$source" -o "$out/stage7"
VPP_HOME="$root" "$out/stage7" check "$probe"
python3 "$root/scripts/quality/check_macos_macho_convergence.py" \
    "$out/stage6" "$out/stage7"

for stage in 6 7; do
    codesign --verify "$out/stage$stage"
    VPP_HOME="$root" "$out/stage$stage" build "$probe" -o "$out/probe$stage"
    set +e
    "$out/probe$stage"
    status=$?
    set -e
    if [[ "$status" != 52 ]]; then
        echo "Stage $stage probe exited $status, expected 52" >&2
        exit 1
    fi
done

# Verify primitive 78 is recognized and linked by the rebuilt compiler.
printf 'hàm chính() { trả về máy đặt quyền tệp mở(-1, 448); };\n' > "$out/fchmod-invalid.vi"
VPP_HOME="$root" "$out/stage7" build "$out/fchmod-invalid.vi" -o "$out/fchmod-invalid"
set +e
"$out/fchmod-invalid"
status=$?
set -e
if [[ "$status" != 255 ]]; then
    echo "fchmod(-1) exited $status, expected 255 (-1)" >&2
    exit 1
fi
echo 'MACOS_FCHMOD_PRIMITIVE=PASS'

# Import resolution and cross-module calls must survive Stage 6/7 bootstrap.
printf 'hàm phụ() { trả về 53; };\n' > "$out/module.vi"
printf 'nhập module.*;\nhàm chính() { trả về phụ(); };\n' > "$out/multi.vi"
VPP_HOME="$root" "$out/stage7" build "$out/multi.vi" -o "$out/multi"
set +e
"$out/multi"
status=$?
set -e
if [[ "$status" != 53 ]]; then
    echo "Multi-module program exited $status, expected 53" >&2
    exit 1
fi
echo 'MACOS_NATIVE_MULTIMODULE=PASS'
echo 'MACOS_NATIVE_SELFHOST_STAGES=PASS (bridge -> stage6 -> stage7)'
