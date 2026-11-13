#!/usr/bin/env bash
# Link full V++ program assembly emitted by the macOS full compiler, with the
# runtime object produced by the self-hosted Linux i32 compiler. No C/C++.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ $# -ne 3 || "$(uname -s)-$(uname -m)" != Linux-x86_64 ]]; then
    echo 'Usage on Linux x86-64: build-linux-full-seed.sh <program.S> <runtime.o> <output>' >&2
    exit 2
fi
source_file=$(realpath "$1")
runtime=$(realpath "$2")
output=$(realpath -m "$3")
[[ -s "$source_file" && -s "$runtime" && ! -d "$output" && "$output" != "$source_file" && "$output" != "$runtime" ]]
if [[ -e "$output" ]]; then
    [[ ! "$output" -ef "$source_file" && ! "$output" -ef "$runtime" ]]
fi
mkdir -p "$(dirname "$output")"
work=$(mktemp -d "$(dirname "$output")/.vpp-linux-link.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
asm="$ROOT_DIR/scripts/ASM"
clang_bin=${VPP_AOT_ASSEMBLER:-$(command -v clang)}
flags=(-target x86_64-unknown-linux-gnu -I"$asm" -I"$asm/common")
"$clang_bin" "${flags[@]}" -DVPP_AOT_VPP_RUNTIME=1 -c "$source_file" -o "$work/program.o"
objects=("$work/program.o")
for part in start syscalls runtime_core directory sha256 process socket aot_runtime_bridge f64; do
    "$clang_bin" "${flags[@]}" -c "$asm/linux/vpp_linux_${part}_x86_64.S" -o "$work/$part.o"
    objects+=("$work/$part.o")
done
"$clang_bin" -target x86_64-unknown-linux-gnu -nostdlib -static -no-pie \
    -Wl,-e,_start -Wl,--no-undefined -Wl,--build-id=none -Wl,-z,noexecstack \
    "${objects[@]}" "$runtime" -o "$work/program"
bash "$ROOT_DIR/scripts/quality/audit_linux_static_elf.sh" "$work/program"
mv -fT "$work/program" "$output"
