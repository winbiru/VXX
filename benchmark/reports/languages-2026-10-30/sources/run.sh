#!/bin/bash
set -euo pipefail
root=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)
output=${1:-"$root/benchmark/reports/languages-2026-10-30"}
build=${VPP_BENCH_BUILD:-"$root/build-language-comparison"}
cli=${VPP_EXEC:-"$root/build-performance/bin/vpp-cli"}
leaf=${VPP_I32_COMPILER:-"$root/build-ci-regression-fix/nativedriver/vpp-native"}
if [ -e "$output" ]; then
  echo "Refusing to overwrite benchmark evidence: $output" >&2
  exit 3
fi
cmake "-DVPP_ROOT=$root" "-DVPP_EXEC=$cli" "-DVPP_BENCH_BUILD=$build" \
  "-DVPP_I32_COMPILER=$leaf" -P "$root/benchmark/languages/build.cmake"
mkdir -p "$output/sources"
cp "$root"/benchmark/languages/* "$output/sources/"
export VPP_HOME="$root" VPP_LANGUAGE_MANIFEST="$build/manifest.json" VPP_LANGUAGE_OUTPUT="$output"
export VPP_ENABLE_JIT=0 VPP_ENABLE_MACHINE_JIT=0 VPP_NATIVE_PROFILE=0
export VPP_LANGUAGE_SMOKE=0
"$build/native-compiler/vpp-native" build "$root/benchmark/languages/run.vi" -o "$build/runner"
mkdir -p "$build/preflight"
VPP_LANGUAGE_OUTPUT="$build/preflight" VPP_LANGUAGE_SMOKE=1 \
  "$build/runner" | tee "$output/preflight.txt"
"$build/runner" | tee "$output/progress.txt"
