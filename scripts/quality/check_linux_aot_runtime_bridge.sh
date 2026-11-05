#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUTPUT_DIR="${1:-$ROOT_DIR/build/native-freestanding-cross}"
CC="${CC:-clang}"
# Apple's system nm is Mach-O-only; LLVM nm reads cross-assembled Linux ELF.
NM=nm
if [[ "$(uname -s)" == Darwin ]]; then
    NM=llvm-nm
fi
for utility in "$CC" python3 "$NM"; do
    if ! command -v "$utility" >/dev/null 2>&1; then
        echo "Missing Linux runtime bridge test tool: $utility" >&2
        exit 2
    fi
done
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
"$CC" -target x86_64-unknown-linux-gnu -c \
    "$ROOT_DIR/scripts/ASM/linux/vpp_linux_aot_runtime_bridge_x86_64.S" \
    "-I$ROOT_DIR/scripts/ASM" -o "$OUTPUT_DIR/bridge.o"
"$NM" -u "$OUTPUT_DIR/bridge.o" > "$OUTPUT_DIR/bridge.undefined"
"$NM" -g "$OUTPUT_DIR/bridge.o" > "$OUTPUT_DIR/bridge.exports"
python3 - "$ROOT_DIR" "$OUTPUT_DIR" <<'PY'
import pathlib
import re
import sys

root, out = map(pathlib.Path, sys.argv[1:])
asm = root / 'scripts/ASM'
canonical = dict(re.findall(r'^\.set (rt_\w+), "(vpp_u_[0-9a-f]+)"$',
    (asm / 'common/vpp_vietnamese_runtime_symbols.inc').read_text(), re.M))
actual = dict(re.findall(r'^#define (rt_\w+) "(vpp_fn_[^"\n]+)"$',
    (asm / 'linux/vpp_linux_aot_runtime_symbols.inc').read_text(), re.M))
exports = re.findall(r'^RT_EXPORT (vpp_native_\w+), (rt_\w+), (\d+)$',
    (asm / 'linux/vpp_linux_aot_runtime_exports.inc').read_text(), re.M)
if len(exports) != 48 or len({name for name, _, _ in exports}) != 48:
    sys.exit('Expected 48 distinct V++ runtime bridge exports')
used = {alias for _, alias, _ in exports}
if len(used) != 48 or set(actual) != used:
    sys.exit('Linux runtime bridge mappings do not exactly cover 48 targets')
if any(actual[alias] != 'vpp_fn_' + bytes.fromhex(canonical[alias][6:]).decode('utf-8')
       for alias in used if alias in canonical) or not used <= canonical.keys():
    sys.exit('Linux ELF vpp_fn mappings do not decode to the canonical V++ function names')
if any(int(count) > 3 for _, _, count in exports):
    sys.exit('RT_EXPORT bridge has more than three supported register inputs')
undefined = {match.group(1) for line in (out / 'bridge.undefined').read_text().splitlines()
             if (match := re.match(r'^\s*U\s+(.+)$', line))}
expected = {'main', 'vpp_fn_vm_f64_parse', 'vpp_fn_vm_f64_text'} | set(actual.values())
if undefined != expected:
    sys.exit(f'Unexpected ELF bridge undefined symbols:\n  missing: {sorted(expected-undefined)}'
             f'\n  additional: {sorted(undefined-expected)}')
symbols = set()
for line in (out / 'bridge.exports').read_text().splitlines():
    parts = line.split()
    if len(parts) >= 3 and parts[-2].upper() == 'T':
        symbols.add(parts[-1])
for public, _, _ in exports:
    if public not in symbols:
        sys.exit(f'Missing exported bridge function {public}')
for public in ['vpp_freestanding_main', 'vpp_native_i32_div',
               'vpp_native_os_register_cache_report', 'vpp_native_f64_parse',
               'vpp_native_f64_text']:
    if public not in symbols:
        sys.exit(f'Missing standalone bridge function {public}')
print('Linux V++ AOT bridge: 48 exports, UTF-8 i32 ELF symbol targets, ABI inputs and cross-assembly OK.')
PY
