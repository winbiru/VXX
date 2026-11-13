#!/usr/bin/env bash
# Reproduce the Linux i32 compiler, assembler and host from text sources and
# one trusted V++ seed that supports `assemble`. No input object is copied.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
[[ $# == 2 && "$(uname -s)-$(uname -m)" == Linux-x86_64 ]] || {
    echo 'Usage: check_linux_source_bootstrap.sh <seed-with-assemble> <fresh-output-dir>' >&2
    exit 2
}
seed="$(realpath "$1")"
[[ -x "$seed" && ! -e "$2" ]] || { echo 'Seed must be executable; output must not exist.' >&2; exit 2; }
mkdir -p "$2"
out="$(realpath "$2")"
root="$out/source"
mkdir -p "$root/scripts/ASM/linux" "$root/scripts/quality" "$root/src/tests/fixtures"
cp -R "$ROOT_DIR/gói" "$root/"
cp "$ROOT_DIR/scripts/ASM/linux/vpp_linux_host_x86_64.s" "$root/scripts/ASM/linux/"
cp -R "$ROOT_DIR/scripts/ASM/linux/tu_chu_x86_64" "$root/scripts/ASM/linux/"
for script in prepare_linux_freestanding_host.sh check_native_elf_linker.sh check_native_elf_partial_linker.py audit_linux_static_elf.sh check_native_assembler.py; do
    cp "$ROOT_DIR/scripts/quality/$script" "$root/scripts/quality/"
done
cp "$ROOT_DIR"/src/tests/fixtures/native_elf*.vi "$root/src/tests/fixtures/"
[[ -z "$(find "$root" -type f \( -name '*.o' -o -name '*.a' -o -name '*.so' -o -name '*.bin' \) -print)" ]]
echo 'SOURCE_BOOTSTRAP_INPUT_OBJECTS=NONE'
mkdir -p "$out/forbid-tools"
export VPP_FORBIDDEN_TOOL_LOG="$out/forbidden-tools.log"
: > "$VPP_FORBIDDEN_TOOL_LOG"
for utility in cc c++ gcc g++ clang clang++ ld ld.lld lld as cpp llvm-mc objcopy; do
    cat > "$out/forbid-tools/$utility" <<'GUARD'
#!/bin/sh
printf '%s\n' "$0 $*" >> "$VPP_FORBIDDEN_TOOL_LOG"
exit 99
GUARD
    chmod +x "$out/forbid-tools/$utility"
done
export PATH="$out/forbid-tools:$PATH"
export VPP_HOME="$root" VPP_NATIVE_ELF_LINK=1 VPP_NATIVE_NO_CACHE=1
cd "$root"
host="$root/scripts/ASM/linux/vpp_aot_primitives_linux_x86_64.o"
VPP_LINUX_HOST_COMPILER="$seed" timeout --kill-after=5 120 \
    bash scripts/quality/prepare_linux_freestanding_host.sh "$host"
cp "$host" "$out/host-seed.o"
echo 'SOURCE_BOOTSTRAP_HOST_FROM_TEXT=PASS'
# The trusted bootstrap seed can predate the ASM platform directories. Only
# its isolated staging workspace gets the former flat path. Both paths refer
# to the object just assembled from V++/ASM text, never a prebuilt input.
ln -s linux/vpp_aot_primitives_linux_x86_64.o \
    "$root/scripts/ASM/vpp_aot_primitives_linux_x86_64.o"
# Test the seed's 2 MiB byte arena and slot isolation rather than guessing
# from its filename. This does not prove its ELF emitter/archive capacity:
# an older seed can pass this check and still reject today's compiler source.
# Self-append reaches exactly 2 MiB in 21 doublings. A second live buffer
# detects a stale 1 MiB slot stride even if its bounds checks allow 2 MiB.
# The final append must fail without changing either buffer.
cat > "$out/seed-buffer-probe.vi" <<'VPP'
hàm chính() {
    biến dữ liệu = byte mới();
    biến canh = byte mới();
    nếu (dữ liệu <= 0 || canh <= 0) { trả về 2; };
    nếu (byte thêm(dữ liệu, 65) != 0) { byte hủy(dữ liệu); trả về 3; };
    nếu (byte thêm(canh, 90) != 0) { byte hủy(dữ liệu); byte hủy(canh); trả về 8; };
    lặp (i = 0; i < 21; i = i + 1) {
        nếu (byte nối(dữ liệu, dữ liệu) != 0) { byte hủy(dữ liệu); trả về 4; };
    };
    nếu (byte dài(dữ liệu) != 2097152) { byte hủy(dữ liệu); trả về 5; };
    // A successful length alone can hide an incorrect self-append or a
    // truncated copy. Inspect both sides of the 1 MiB boundary and the end.
    nếu (byte lấy(dữ liệu, 0) != 65 || byte lấy(dữ liệu, 1048575) != 65 ||
         byte lấy(dữ liệu, 1048576) != 65 || byte lấy(dữ liệu, 2097151) != 65) {
        byte hủy(dữ liệu); byte hủy(canh); trả về 10;
    };
    nếu (byte thêm(dữ liệu, 66) == 0) { byte hủy(dữ liệu); trả về 6; };
    nếu (byte dài(dữ liệu) != 2097152) { byte hủy(dữ liệu); trả về 7; };
    nếu (byte lấy(dữ liệu, 0) != 65 || byte lấy(dữ liệu, 2097151) != 65) {
        byte hủy(dữ liệu); byte hủy(canh); trả về 11;
    };
    nếu (byte dài(canh) != 1 || byte lấy(canh, 0) != 90) { byte hủy(dữ liệu); byte hủy(canh); trả về 9; };
    byte hủy(dữ liệu);
    byte hủy(canh);
    trả về 0;
};
VPP
if ! timeout --kill-after=5 120 "$seed" build "$out/seed-buffer-probe.vi" \
    -o "$out/seed-buffer-probe" > "$out/seed-buffer-probe.log" 2>&1; then
    tail -30 "$out/seed-buffer-probe.log" >&2
    echo 'SOURCE_BOOTSTRAP_SEED_ARENA_2M=FAIL (seed cannot build the probe)' >&2
    exit 1
fi
if ! timeout --kill-after=5 60 "$out/seed-buffer-probe"; then
    echo 'SOURCE_BOOTSTRAP_SEED_ARENA_2M=FAIL (seed has an obsolete or overlapping byte arena)' >&2
    exit 1
fi
echo 'SOURCE_BOOTSTRAP_SEED_ARENA_2M=PASS'
if bash scripts/quality/check_native_elf_linker.sh "$seed" "$out/selfhost" 2>&1 | tee "$out/selfhost.log"; then
    :
else
    status=$?
    if grep -q 'compiler exit=16' "$out/selfhost.log"; then
        echo 'SOURCE_BOOTSTRAP_SEED_ELF_ARCHIVE=FAIL: the seed could not emit/archive the current compiler. Upgrade to a verified 2 MiB ELF-capable seed; the byte-arena probe alone is insufficient.' >&2
    fi
    exit "$status"
fi
rm "$root/scripts/ASM/vpp_aot_primitives_linux_x86_64.o"
# Check that the newly self-hosted compiler resolves only the organized
# Linux path, with the compatibility alias removed from the source tree.
timeout --kill-after=5 120 "$out/selfhost/vpp-native-stage2" build \
    "$out/selfhost/smoke.vi" -o "$out/smoke-platform-dir-only" \
    > "$out/smoke-platform-dir-only.log" 2>&1
cmp "$out/selfhost/smoke-final" "$out/smoke-platform-dir-only"
set +e
timeout --kill-after=5 30 "$out/smoke-platform-dir-only"
status=$?
set -e
[[ "$status" -eq 42 ]]
echo 'SOURCE_BOOTSTRAP_PLATFORM_DIR_ONLY=PASS'
# The implicit runtime uses an absolute path. A relative entry importing
# that same runtime must not collect its declarations a second time.
for source in 'gói/biên dịch/bản địa/bộ biên dịch.vi' './gói/biên dịch/bản địa/bộ biên dịch.vi'; do
    timeout --kill-after=5 600 "$out/selfhost/vpp-native-stage2" check "$source"
done
echo 'SOURCE_BOOTSTRAP_RELATIVE_MODULE_PATH=PASS'
for stage in 1 2; do
    compiler="$out/selfhost/vpp-native-stage$stage"
    VPP_LINUX_HOST_COMPILER="$compiler" timeout --kill-after=5 120 \
        bash scripts/quality/prepare_linux_freestanding_host.sh "$out/host-stage$stage.o"
    cmp "$out/host-seed.o" "$out/host-stage$stage.o"
done
python3 scripts/quality/check_native_assembler.py "$out/selfhost/vpp-native-stage2" \
    "$out/selfhost/native_elf_linker_archive" "$out/assembler-regressions"
[[ ! -s "$out/forbidden-tools.log" && ! -s "$out/selfhost/forbidden-tools.log" ]]
sha256sum "$out/host-seed.o" "$out/host-stage1.o" "$out/host-stage2.o"
echo 'SOURCE_BOOTSTRAP_HOST_EXACT_CMP=0'
echo 'SOURCE_BOOTSTRAP_COMPILER_ASSEMBLER_LINKER=VPP'
echo 'SOURCE_BOOTSTRAP_EXTERNAL_BUILD_TOOLS=NONE'
echo 'SOURCE_BOOTSTRAP_CONVERGENCE=PASS'
