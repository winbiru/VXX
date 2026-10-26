#!/usr/bin/env bash
# Build native V++ and C++ kernels independently, then time identical calls.
# macOS arm64 only: the V++ object backend below emits Mach-O AArch64.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
output_dir="${1:-${repo_root}/build-benchmark/native-comparison-current}"
rounds="${2:-7}"
target_seconds="${3:-0.03}"
object_mode="${VPP_OBJECT_MODE:---emit-obj-selfhost}"
compiler="${VPP_EXEC:-${repo_root}/build/bin/vpp-cli}"
# Prefer the bootstrapped V++ CLI for direct object compilation. Explicit
# VPP_EXEC and the legacy --emit-obj comparison keep their existing meaning.
if [[ -z "${VPP_EXEC:-}" && "${object_mode}" == "--emit-obj-selfhost" &&
      -x "${repo_root}/build/native-cli/vpp-native" ]]; then
  compiler="${repo_root}/build/native-cli/vpp-native"
fi
source_dir="${repo_root}/benchmark/native"

if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
  echo "This benchmark currently requires macOS arm64." >&2
  exit 1
fi
if [[ ! -x "${compiler}" ]]; then
  echo "Missing ${compiler}; build the vpp CLI first." >&2
  exit 1
fi
if [[ "${object_mode}" != "--emit-obj-selfhost" && "${object_mode}" != "--emit-obj" ]]; then
  echo "Unsupported VPP_OBJECT_MODE: ${object_mode}" >&2
  exit 2
fi

mkdir -p "${output_dir}"
for kernel in vpp_arithmetic vpp_cycle vpp_fibonacci; do
  "${compiler}" build --target aarch64-apple-darwin "${object_mode}" \
    "${kernel}" "${source_dir}/kernels.vi" "${output_dir}/${kernel}.o"
done

for level in 0 2 3; do
  clang++ -std=c++17 "-O${level}" -fno-lto \
    "-DBENCH_PREFIX=cpp_o${level}" -c "${source_dir}/kernels.cpp" \
    -o "${output_dir}/cpp_O${level}.o"
done

clang++ -std=c++17 -O3 -fno-lto "${source_dir}/harness.cpp" \
  "${output_dir}/vpp_arithmetic.o" "${output_dir}/vpp_cycle.o" \
  "${output_dir}/vpp_fibonacci.o" "${output_dir}/cpp_O0.o" \
  "${output_dir}/cpp_O2.o" "${output_dir}/cpp_O3.o" \
  -o "${output_dir}/native-comparison"

"${output_dir}/native-comparison" "${rounds}" "${target_seconds}" > "${output_dir}/results.json"
otool -L "${output_dir}/native-comparison" > "${output_dir}/dependencies.txt"
nm -u "${output_dir}/native-comparison" > "${output_dir}/undefined-symbols.txt"
otool -L "${compiler}" > "${output_dir}/compiler-dependencies.txt"
otool -tvV "${output_dir}/vpp_arithmetic.o" "${output_dir}/vpp_cycle.o" "${output_dir}/vpp_fibonacci.o" \
  "${output_dir}/cpp_O3.o" > "${output_dir}/disassembly.txt"
{
  date -u '+UTC: %Y-%m-%dT%H:%M:%SZ'
  sw_vers -productVersion
  uname -m
  clang++ --version
  echo "V++ compiler: ${compiler}"
  echo "V++ object mode: ${object_mode}"
  shasum -a 256 "${compiler}" "${source_dir}/kernels.vi" \
    "${source_dir}/kernels.cpp" "${source_dir}/harness.cpp"
} > "${output_dir}/environment.txt"

echo "Benchmark results: ${output_dir}/results.json"
echo "Dependencies / disassembly: ${output_dir}/"
