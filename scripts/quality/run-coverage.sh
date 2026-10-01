#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${VPP_COVERAGE_BUILD_DIR:-$ROOT_DIR/build-coverage}"
MINIMUM="${VPP_COVERAGE_MINIMUM:-45}"

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

ctest --test-dir "$BUILD_DIR" --output-on-failure --no-tests=error

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
