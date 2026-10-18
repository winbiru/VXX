#!/usr/bin/env bash
# Build V++ native leaf ELF objects, link with a real target C ABI,
# then execute the machine code under QEMU (or directly on a host).
set -euo pipefail

if [[ $# -ne 5 ]]; then
  echo "usage: $0 <vpp-cli> <target-triple> <target-cc> <qemu-user|native> <output-dir>" >&2
  exit 2
fi

vpp_exec="$1"
target="$2"
target_cc="$3"
runner="$4"
output_dir="$5"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if [[ ! -x "$vpp_exec" ]]; then
  echo "missing executable V++ compiler: $vpp_exec" >&2
  exit 2
fi
if ! command -v "$target_cc" >/dev/null 2>&1; then
  echo "missing cross C compiler: $target_cc" >&2
  exit 2
fi
if [[ "$runner" != native ]] && ! command -v "$runner" >/dev/null 2>&1; then
  echo "missing emulator: $runner" >&2
  exit 2
fi

mkdir -p "$output_dir"
objects=()
for function in tra42 tra_am tra_i32 tra_i32_am tra_i32_am_ngoac tinh_hang tinh_am tinh_sau cong cong_dao tru; do
  "$vpp_exec" build --target "$target" --emit-obj "$function" \
    "$repo_root/src/tests/kiem_tra_native_leaf.vi" \
    "$output_dir/$function.o"
  objects+=("$output_dir/$function.o")
done

if [[ "$runner" == native ]]; then
  # The host may be macOS; Darwin does not support GCC's -static flag.
  "$target_cc" -O0 -fno-inline \
    "$repo_root/scripts/quality/native_cross_object_smoke.c" \
    "${objects[@]}" \
    -o "$output_dir/native-cross-smoke"
  "$output_dir/native-cross-smoke"
else
  # Static linking makes the emulator test independent of target rootfs/glibc.
  "$target_cc" -static -O0 -fno-inline \
    "$repo_root/scripts/quality/native_cross_object_smoke.c" \
    "${objects[@]}" \
    -o "$output_dir/native-cross-smoke"
  if command -v timeout >/dev/null 2>&1; then
    timeout 30s "$runner" "$output_dir/native-cross-smoke"
  else
    "$runner" "$output_dir/native-cross-smoke"
  fi
fi

echo "PASS: $target native objects linked by $target_cc and executed via $runner"
