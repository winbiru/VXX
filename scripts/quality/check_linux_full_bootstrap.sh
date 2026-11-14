#!/usr/bin/env bash
# Bootstrap the full compiler on Linux from the pinned x86-64 assembly seed
# and a Linux i32 compiler. No macOS executable or CI artifact is used.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT_DIR/scripts/build-vpp-progress.sh"
phase=all
if [[ ${1:-} == --phase ]]; then
    phase=${2:-}
    shift 2
fi
case "$phase" in
    all|prepare|stage1|stage2|stage3|convergence|test) ;;
    *) echo "Unknown Linux bootstrap phase: $phase" >&2; exit 2 ;;
esac
if [[ $# -lt 1 || $# -gt 2 || "$(uname -s)-$(uname -m)" != Linux-x86_64 || -z "$1" ]]; then
    echo 'Usage on Linux x86-64: check_linux_full_bootstrap.sh [--phase PHASE] <output-dir> [linux-i32-prefix]' >&2
    exit 2
fi
for tool in clang readelf nm sha256sum gzip realpath timeout python3; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing required Linux bootstrap tool: $tool" >&2
        exit 2
    fi
done
# Keep the bootstrap independent of any caller's installed V++ toolchain,
# foreign target, cached output, or alternate runtime. Preserve only the
# documented fixture timeout override.
for name in ${!VPP_@}; do
    if [[ "$name" != VPP_LINUX_FIXTURE_TIMEOUT_SECONDS ]]; then unset "$name"; fi
done
seed_dir="$ROOT_DIR/bootstrap/linux-x86_64"
progress() { printf '[Biên dịch V++ Linux đầy đủ %3d%%] %s\n' "$1" "$2" >&2; }
if [[ "$phase" == all || "$phase" == prepare ]]; then
    if [[ -e "$1" || -L "$1" ]]; then
        echo "A fresh Linux bootstrap output directory is required" >&2
        exit 2
    fi
    for seed in vpp-i32 vpp-full.S.gz; do
        if [[ $(awk -v seed="$seed" '$2 == seed && length($1) == 64 && $1 ~ /^[0-9a-f]+$/ { count++ } END { print count+0 }' "$seed_dir/SHA256SUMS") != 1 ]]; then
            echo "Missing or duplicate SHA-256 pin for $seed" >&2
            exit 1
        fi
    done
    (cd "$seed_dir" && sha256sum -c --strict SHA256SUMS)
    gzip -t "$seed_dir/vpp-full.S.gz"
    if [[ $# == 2 ]]; then
        prefix="$(realpath "$2")"
        if [[ ! -x "$prefix/bin/vpp-i32" || ! -s "$prefix/scripts/ASM/linux/vpp_aot_primitives_linux_x86_64.o" ]]; then
            echo 'Invalid Linux i32 prefix: compiler and ASM host object are required.' >&2
            exit 2
        fi
    fi
    mkdir -p "$(dirname "$1")"
    mkdir "$1"
    out="$(realpath "$1")"

    progress 0 'Chuẩn bị bộ biên dịch i32 Linux'
    mkdir -p "$out/i32/bin" "$out/i32/scripts/ASM/linux"
    host_relative=scripts/ASM/linux/vpp_aot_primitives_linux_x86_64.o
    # Always use the current checkout, even if the supplied i32 prefix is older.
    cp -R "$ROOT_DIR/gói" "$out/i32/"
    if [[ $# == 2 ]]; then
        install -m 755 "$prefix/bin/vpp-i32" "$out/i32/bin/vpp-i32"
        install -m 644 "$prefix/$host_relative" "$out/i32/$host_relative"
    else
        # Standalone local entry point: reproduce i32 and its host from the
        # repository seed, just as the Linux i32 CI job does.
        bash "$ROOT_DIR/scripts/quality/check_linux_source_bootstrap.sh" "$seed_dir/vpp-i32" "$out/i32-bootstrap"
        install -m 755 "$out/i32-bootstrap/selfhost/vpp-native-stage2" "$out/i32/bin/vpp-i32"
        install -m 644 "$out/i32-bootstrap/host-stage2.o" "$out/i32/$host_relative"
    fi
    bash "$ROOT_DIR/scripts/quality/audit_linux_static_elf.sh" "$out/i32/bin/vpp-i32"
fi
out="$(realpath "$1")"
test -d "$out"
cd "$ROOT_DIR"
export VPP_HOME="$out/i32" VPP_NATIVE_ELF_LINK=1 VPP_NATIVE_NO_CACHE=1 VPP_NATIVE_BUILD_PROGRESS=1
export VPP_AOT_ASSEMBLER="$(command -v clang)"
export VPP_AOT_NATIVE_MACHO_LINK=0 VPP_AOT_NATIVE_ARM64_OBJECT=0
if [[ "$phase" == all || "$phase" == prepare ]]; then
    progress 10 'Biên dịch bộ thực thi V++ từ mã nguồn'
    vpp_timed_command "$out/i32/bin/vpp-i32" runtime-object \
        "$VPP_HOME/gói/máy ảo/runtime syscall.vi" -o "$out/runtime.o" 2>&1 | tee "$out/runtime-build.log"
fi
if [[ "$phase" == all || "$phase" == stage1 ]]; then
    progress 25 'Hợp dịch và liên kết seed Linux x86-64 trong repository'
    gzip -dc "$seed_dir/vpp-full.S.gz" > "$out/seed.S"
    bash "$ROOT_DIR/scripts/linux/build-linux-full-seed.sh" "$out/seed.S" "$out/runtime.o" "$out/vpp-native" 2>&1 | tee "$out/stage1-build.log"
    progress 35 'Kiểm tra thực thi compiler seed Linux'
    bash "$ROOT_DIR/scripts/quality/check_linux_dynamic_vpp.sh" "$out/fixtures" "$out/vpp-native" "$out/runtime.o"
fi
export VPP_HOME="$ROOT_DIR" VPP_AOT_VPP_OBJECT="$out/runtime.o"
source_file="$ROOT_DIR/gói/biên dịch/dòng lệnh AOT tự chủ.vi"
if [[ "$phase" == all || "$phase" == stage2 ]]; then
    progress 45 'Tự biên dịch bộ biên dịch giai đoạn 2'
    vpp_profile_command "$out/stage2-profile.log" "$out/vpp-native" build "$source_file" -o "$out/vpp-native-stage2" 2>&1 | tee "$out/stage2-build.log"
    bash "$ROOT_DIR/scripts/quality/check_linux_dynamic_vpp.sh" "$out/stage2-fixtures" "$out/vpp-native-stage2" "$out/runtime.o"
fi
if [[ "$phase" == all || "$phase" == stage3 ]]; then
    progress 65 'Tự biên dịch bộ biên dịch giai đoạn 3'
    vpp_profile_command "$out/stage3-profile.log" "$out/vpp-native-stage2" build "$source_file" -o "$out/vpp-native-stage3" 2>&1 | tee "$out/stage3-build.log"
fi
if [[ "$phase" == all || "$phase" == convergence ]]; then
    progress 80 'Kiểm tra tính đồng nhất của hai thế hệ bộ biên dịch'
    bash "$ROOT_DIR/scripts/quality/audit_linux_static_elf.sh" "$out/vpp-native-stage3"
    cmp "$out/vpp-native-stage2" "$out/vpp-native-stage3"
    (cd "$out" && sha256sum vpp-native-stage2 vpp-native-stage3 > convergence.sha256)
    echo 'LINUX_FULL_BOOTSTRAP_CONVERGENCE=PASS'
fi
if [[ "$phase" == all || "$phase" == test ]]; then
    bash "$ROOT_DIR/scripts/quality/check_linux_dynamic_vpp.sh" "$out/stage3-fixtures" "$out/vpp-native-stage3" "$out/runtime.o"
fi
