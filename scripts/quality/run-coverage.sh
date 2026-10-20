#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${VPP_COVERAGE_BUILD_DIR:-$ROOT_DIR/build-coverage}"
MINIMUM="${VPP_COVERAGE_MINIMUM:-45}"
CTEST_PARALLEL="${VPP_COVERAGE_CTEST_PARALLEL:-2}"

STAGE="prerequisites"
STAGE_STARTED=$SECONDS
report_failure() {
  local exit_code="$1"
  local failed_command="$2"
  local log
  trap - ERR
  printf '[coverage] FAILED at %s after %ss (exit %s): %s\n' \
    "$STAGE" "$((SECONDS - STAGE_STARTED))" "$exit_code" "$failed_command" >&2
  case "$STAGE" in
    capture) log="$BUILD_DIR/coverage.capture.log" ;;
    filter) log="$BUILD_DIR/coverage.filter.log" ;;
    *) log="" ;;
  esac
  if [ -n "$log" ] && [ -f "$log" ]; then
    printf '[coverage] Last 80 lines from %s:\n' "$log" >&2
    tail -n 80 "$log" >&2
  fi
  exit "$exit_code"
}
report_duration() {
  printf '[coverage] %s completed in %ss\n' "$STAGE" "$((SECONDS - STAGE_STARTED))"
}
trap 'report_failure "$?" "$BASH_COMMAND"' ERR

command -v cmake >/dev/null
command -v lcov >/dev/null
printf '[coverage] %s\n' "$(lcov --version | sed -n '1p')"

STAGE="configure"
STAGE_STARTED=$SECONDS
echo '[coverage] Configure instrumented C++ build'
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DVPP_ENABLE_COVERAGE=ON \
  -DBUILD_TESTS=ON \
  -DBUILD_BENCHMARKS=OFF
report_duration
STAGE="build"
STAGE_STARTED=$SECONDS
echo '[coverage] Build instrumented C++ targets'
cmake --build "$BUILD_DIR" --parallel
report_duration

# Coverage counters belong to the current test run only. Removing stale gcda
# files also makes repeated local runs deterministic after source/flag changes.
find "$BUILD_DIR" -type f -name '*.gcda' -delete

# Native compiler, machine-code, and self-hosting checks run against the
# uninstrumented Release toolchain in the mandatory build/ARM64 jobs. Running
# them against an instrumented Stage 0 triggers repeated native linking and
# bootstrap while providing little usable C++ coverage.
# The instrumented HTTP fixture compiles its network modules before listening.
STAGE="ctest"
STAGE_STARTED=$SECONDS
echo '[coverage] Run instrumented CTest suite'
VPP_HTTP_FIXTURE_TIMEOUT="${VPP_HTTP_FIXTURE_TIMEOUT:-300}" \
  ctest --test-dir "$BUILD_DIR" --parallel "$CTEST_PARALLEL" \
    --output-on-failure --no-tests=error --output-log "$BUILD_DIR/ctest.log" \
    -E '^(vpp-native-|vpp-raw-machine-cli-hardening$|vpp-raw-object-host-link$|vpp-compiler-vpp-cli$|vpp-oop-pipeline$|vpp-compiler-ffi-artifact$)'
report_duration

STAGE="capture"
STAGE_STARTED=$SECONDS
echo '[coverage] Capture gcov counters'
# LCOV/GCC can enumerate thousands of system headers. Preserve the complete
# logs in CI artifacts, and print their final diagnostics if either step fails.
lcov --capture --directory "$BUILD_DIR" \
  --rc geninfo_unexecuted_blocks=1 \
  --output-file "$BUILD_DIR/coverage.raw.info" \
  >"$BUILD_DIR/coverage.capture.log" 2>&1
report_duration
STAGE="filter"
STAGE_STARTED=$SECONDS
echo '[coverage] Filter third-party, generated, and test source files'
lcov --remove "$BUILD_DIR/coverage.raw.info" \
  --ignore-errors unused \
  '/usr/*' \
  '*/test/*' \
  '*/src/tests/*' \
  '*/examples/*' \
  '*/templates/*' \
  '*/build*/*' \
  --output-file "$BUILD_DIR/coverage.info" \
  >"$BUILD_DIR/coverage.filter.log" 2>&1
report_duration

STAGE="gate"
STAGE_STARTED=$SECONDS
echo '[coverage] Evaluate line coverage gate'
read -r FOUND HIT <<EOF
$(awk -F: '
  /^LF:/ { found += $2 }
  /^LH:/ { hit += $2 }
  END { printf "%d %d\n", found, hit }
' "$BUILD_DIR/coverage.info")
EOF

if [ "$FOUND" -eq 0 ]; then
  echo "[coverage] ERROR: no instrumented lines were found; inspect coverage.filter.log" >&2
  exit 2
fi

PERCENT=$(awk -v hit="$HIT" -v found="$FOUND" 'BEGIN { printf "%.2f", hit * 100.0 / found }')
echo "coverage: $HIT/$FOUND lines = $PERCENT% (minimum $MINIMUM%)"
if ! awk -v hit="$HIT" -v found="$FOUND" -v minimum="$MINIMUM" \
  'BEGIN { exit !(hit * 100.0 >= found * minimum) }'; then
  echo "[coverage] ERROR: line coverage $PERCENT% is below the $MINIMUM% minimum" >&2
  exit 1
fi
echo '[coverage] PASS'
