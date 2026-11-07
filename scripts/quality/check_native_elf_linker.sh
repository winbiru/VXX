#!/usr/bin/env bash
# Exercise the V++ ET_EXEC linker with external compiler/linker commands blocked.
# The seed must already recognize primitive 78 (fchmod). The host .o must have
# been prepared once with prepare_linux_freestanding_host.sh.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ $# -ne 2 ]]; then
    echo 'Usage: check_native_elf_linker.sh <seed-compiler> <output-dir>' >&2
    exit 2
fi
seed="$1"
output_dir="$2"
[[ "$(uname -s)-$(uname -m)" == Linux-x86_64 && -x "$seed" ]] || exit 2
[[ -s "$ROOT_DIR/scripts/ASM/linux/vpp_aot_primitives_linux_x86_64.o" ]] || {
    echo 'Prepare the freestanding host object before this gate.' >&2
    exit 2
}
mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd)"
export VPP_HOME="$ROOT_DIR"
export VPP_NATIVE_ELF_LINK=1
mkdir -p "$output_dir/forbid-tools"
export VPP_FORBIDDEN_TOOL_LOG="$output_dir/forbidden-tools.log"
: > "$VPP_FORBIDDEN_TOOL_LOG"
for utility in cc c++ gcc g++ clang clang++ ld ld.lld lld as; do
    cat > "$output_dir/forbid-tools/$utility" <<'GUARD'
#!/bin/sh
printf '%s\n' "$0 $*" >> "$VPP_FORBIDDEN_TOOL_LOG"
echo 'Unexpected external build tool invocation' >&2
exit 99
GUARD
    chmod +x "$output_dir/forbid-tools/$utility"
done
export PATH="$output_dir/forbid-tools:$PATH"
compile() {
    local compiler="$1" source="$2" executable="$3" log="$4"
    if timeout --kill-after=10 "${VPP_SELFHOST_STAGE_TIMEOUT_SECONDS:-900}" \
        "$compiler" build "$source" -o "$executable" > "$log" 2>&1; then
        :
    else
        local exit_code=$?
        tail -60 "$log" >&2
        printf '[native ELF] compiler exit=%d, source=%s\n' "$exit_code" "$source" >&2
        return "$exit_code"
    fi
    bash "$ROOT_DIR/scripts/quality/audit_linux_static_elf.sh" "$executable"
    if readelf -W -l "$executable" | awk '$1 == "LOAD" && /W/ && /E/ {bad=1} END {exit !bad}'; then
        echo 'Writable executable LOAD segment' >&2
        return 1
    fi
}
printf 'hàm chính() { trả về 42; };\n' > "$output_dir/smoke.vi"
compile "$seed" "$output_dir/smoke.vi" "$output_dir/smoke" "$output_dir/smoke.log"
set +e
timeout --kill-after=5 30 "$output_dir/smoke"
status=$?
set -e
[[ "$status" -eq 42 ]]
echo 'NATIVE_ELF_SMOKE_EXIT=42'
compile "$seed" "$ROOT_DIR/src/tests/fixtures/native_elf_linker_runtime.vi" \
    "$output_dir/runtime-smoke" "$output_dir/runtime-smoke.log"
set +e
timeout --kill-after=5 30 "$output_dir/runtime-smoke"
status=$?
set -e
[[ "$status" -eq 42 ]]
echo 'NATIVE_ELF_RUNTIME_SMOKE_EXIT=42'
# A source-only leaf object must omit all imported runtime units and remain
# writable directly, without falling back to ld -r after the runtime split.
if ! timeout --kill-after=10 120 "$seed" object "$output_dir/smoke.vi" \
    -o "$output_dir/source-only.o" > "$output_dir/object.log" 2>&1; then
    tail -60 "$output_dir/object.log" >&2
    exit 1
fi
defined_symbols="$(nm --defined-only "$output_dir/source-only.o")"
if [[ "$defined_symbols" != *' T main' || "$defined_symbols" == *vpp_fn_* ]]; then
    echo 'Source-only object contains unexpected runtime definitions.' >&2
    exit 1
fi
echo 'NATIVE_ELF_SOURCE_ONLY_OBJECT=PASS'
# The source compiler must emit a canonical non-executable stack note in
# every ET_REL. Inspect the actual compiler output, not a synthetic fixture.
python3 - "$output_dir/source-only.o" <<'PY'
from pathlib import Path
import struct
import sys

data = Path(sys.argv[1]).read_bytes()
assert data[:7] == b"\x7fELF\x02\x01\x01"
assert struct.unpack_from("<HH", data, 16) == (1, 62)
table = struct.unpack_from("<Q", data, 40)[0]
entry_size, count, string_index = struct.unpack_from("<HHH", data, 58)
assert entry_size == 64 and count == 7 and string_index == 5
assert table + count * entry_size == len(data)

def section(i):
    return struct.unpack_from("<IIQQQQIIQQ", data, table + i * entry_size)

name_table = section(string_index)
start, length = name_table[4:6]
names = data[start:start + length]
assert name_table[1] == 3 and len(names) == length
note = section(6)
offset = note[0]
assert names[offset: names.index(0, offset)] == b".note.GNU-stack"
assert note[1] == 1 and note[2] == 0 and note[5] == 0
assert note[8] == 1 and note[9] == 0
print("NATIVE_ELF_SOURCE_ONLY_NOEXEC_STACK=PASS")
PY
compiler="$seed"
# Use the complete multi-platform entrypoint to test the enlarged archive;
# retain the Linux-only entrypoint as the default for older bootstrap seeds.
compiler_source="${VPP_LINUX_COMPILER_SOURCE:-$ROOT_DIR/gói/biên dịch/bản địa/bộ biên dịch linux.vi}"
[[ -f "$compiler_source" ]] || { echo "Missing compiler source: $compiler_source" >&2; exit 2; }
for stage in 1 2; do
    executable="$output_dir/vpp-native-stage$stage"
    echo "[native ELF] Self-host stage $stage"
    compile "$compiler" "$compiler_source" \
        "$executable" "$output_dir/stage$stage.log"
    compiler="$executable"
done
cmp "$output_dir/vpp-native-stage1" "$output_dir/vpp-native-stage2"
sha256sum "$output_dir/vpp-native-stage1" "$output_dir/vpp-native-stage2"
echo 'NATIVE_ELF_SELFHOST_EXACT_CMP=0'
# Exercise the native ET_REL -> ET_REL path with the converged compiler.
# Both harnesses use the in-tree V++ linker and the same preassembled host
# object as the self-hosting smoke; the Python fixtures need no assembler.
for harness in native_elf_partial_linker_archive native_elf_linker_archive; do
    compile "$compiler" "$ROOT_DIR/src/tests/fixtures/$harness.vi" \
        "$output_dir/$harness" "$output_dir/$harness.log"
done
python3 "$ROOT_DIR/scripts/quality/check_native_elf_partial_linker.py" \
    "$output_dir/native_elf_partial_linker_archive" \
    "$output_dir/native_elf_linker_archive" \
    "$output_dir/elf-partial-regressions"
compile "$compiler" "$output_dir/smoke.vi" "$output_dir/smoke-final" "$output_dir/smoke-final.log"
set +e
timeout --kill-after=5 30 "$output_dir/smoke-final"
status=$?
set -e
[[ "$status" -eq 42 ]]
[[ ! -s "$VPP_FORBIDDEN_TOOL_LOG" ]]
echo 'NATIVE_ELF_EXTERNAL_BUILD_TOOLS=NONE'
echo 'NATIVE_ELF_SELFHOST_CONVERGENCE=PASS'
