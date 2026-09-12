#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${VPP_COVERAGE_BUILD_DIR:-$ROOT_DIR/build-coverage}"
MINIMUM="${VPP_COVERAGE_MINIMUM:-45}"

command -v cmake >/dev/null
command -v lcov >/dev/null
command -v python3 >/dev/null

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DVPP_ENABLE_COVERAGE=ON \
  -DBUILD_TESTS=ON \
  -DBUILD_BENCHMARKS=OFF
cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure --no-tests=error

lcov --capture --directory "$BUILD_DIR" --output-file "$BUILD_DIR/coverage.raw.info"
lcov --remove "$BUILD_DIR/coverage.raw.info" \
  '/usr/*' \
  '*/test/*' \
  '*/src/tests/*' \
  '*/examples/*' \
  '*/templates/*' \
  '*/build*/*' \
  --output-file "$BUILD_DIR/coverage.info"

python3 "$ROOT_DIR/scripts/quality/check_coverage.py" \
  "$BUILD_DIR/coverage.info" --minimum "$MINIMUM"
