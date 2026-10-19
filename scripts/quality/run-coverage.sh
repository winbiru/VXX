#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${VPP_COVERAGE_BUILD_DIR:-$ROOT_DIR/build-coverage}"
MINIMUM="${VPP_COVERAGE_MINIMUM:-45}"
CTEST_PARALLEL="${VPP_COVERAGE_CTEST_PARALLEL:-2}"

command -v cmake >/dev/null
command -v lcov >/dev/null

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DVPP_ENABLE_COVERAGE=ON \
  -DBUILD_TESTS=ON \
  -DBUILD_BENCHMARKS=OFF
cmake --build "$BUILD_DIR" --parallel

# Coverage counters belong to the current test run only. Removing stale gcda
# files also makes repeated local runs deterministic after source/flag changes.
find "$BUILD_DIR" -type f -name '*.gcda' -delete

# Native compiler, machine-code, and self-hosting checks run against the
# uninstrumented Release toolchain in the mandatory build/ARM64 jobs. Running
# them against an instrumented Stage 0 triggers repeated native linking and
# bootstrap while providing little usable C++ coverage.
# The instrumented HTTP fixture compiles its network modules before listening.
VPP_HTTP_FIXTURE_TIMEOUT="${VPP_HTTP_FIXTURE_TIMEOUT:-300}" \
  ctest --test-dir "$BUILD_DIR" --parallel "$CTEST_PARALLEL" \
    --output-on-failure --no-tests=error \
    -E '^(vpp-native-|vpp-raw-machine-cli-hardening$|vpp-raw-object-host-link$|vpp-compiler-vpp-cli$|vpp-oop-pipeline$|vpp-compiler-ffi-artifact$)'

lcov --capture --directory "$BUILD_DIR" \
  --rc geninfo_unexecuted_blocks=1 \
  --output-file "$BUILD_DIR/coverage.raw.info"
lcov --remove "$BUILD_DIR/coverage.raw.info" \
  --ignore-errors unused \
  '/usr/*' \
  '*/test/*' \
  '*/src/tests/*' \
  '*/examples/*' \
  '*/templates/*' \
  '*/build*/*' \
  --output-file "$BUILD_DIR/coverage.info"

read -r FOUND HIT <<EOF
$(awk -F: '
  /^LF:/ { found += $2 }
  /^LH:/ { hit += $2 }
  END { printf "%d %d\n", found, hit }
' "$BUILD_DIR/coverage.info")
EOF

if [ "$FOUND" -eq 0 ]; then
  echo "coverage: no instrumented lines were found" >&2
  exit 2
fi

PERCENT=$(awk -v hit="$HIT" -v found="$FOUND" 'BEGIN { printf "%.2f", hit * 100.0 / found }')
echo "coverage: $HIT/$FOUND lines = $PERCENT% (minimum $MINIMUM%)"
awk -v actual="$PERCENT" -v minimum="$MINIMUM" 'BEGIN { exit !(actual + 0 >= minimum + 0) }'
