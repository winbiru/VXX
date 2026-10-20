#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${VPP_COVERAGE_BUILD_DIR:-$ROOT_DIR/build-coverage}"
MINIMUM="${VPP_COVERAGE_MINIMUM:-45}"
CTEST_PARALLEL="${VPP_COVERAGE_CTEST_PARALLEL:-2}"

STAGE="prerequisites"
STAGE_STARTED=$SECONDS
FILTER_OUTPUT=""
trap 'if [ -n "$FILTER_OUTPUT" ]; then rm -f "$FILTER_OUTPUT"; fi' EXIT
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
    printf '[coverage] First diagnostics from %s (if any):\n' "$log" >&2
    grep -nEi 'error:|fatal:|failed|traceback' "$log" | head -n 20 >&2 || true
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
command -v awk >/dev/null
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
# LCOV --remove can fail on newer distributions while processing thousands of
# system headers. Keep records from this repository's production src/ tree.
# Stream records so system headers do not require large in-memory buffers;
# preserve accepted records, including LF/LH and branch coverage.
FILTER_OUTPUT="$BUILD_DIR/coverage.info.tmp.$$"
rm -f "$BUILD_DIR/coverage.info"
awk -v source_root="$ROOT_DIR/src/" '
  function fail(reason) {
    print "[coverage] filter error: " reason > "/dev/stderr"
    invalid = 1
  }
  function flush_record(  lines, hits) {
    if (source == "") {
      fail("LCOV record missing SF")
      return
    }
    if (!eligible) return
    if (line_fields != 1 || hit_fields != 1 ||
        line_total !~ /^[0-9]+$/ || line_hit !~ /^[0-9]+$/) {
      fail("invalid LF/LH fields for " source)
      return
    }
    lines = line_total + 0
    hits = line_hit + 0
    if (hits > lines) {
      fail("LH exceeds LF for " source)
      return
    }
    kept++
    found += lines
    covered += hits
  }
  {
    line = $0
    sub(/\r$/, "", line)
    if (!inside && line == "") next
    inside = 1
    if (index(line, "SF:") == 1) {
      if (source != "") fail("duplicate SF field")
      source = substr(line, 4)
      relative = index(source, source_root) == 1 ? substr(source, length(source_root) + 1) : ""
      eligible = relative != "" &&
        relative !~ /(^|\/)(test|tests|examples|templates|build[^\/]*)\//
      if (eligible) printf "%s", prefix
    }
    if (eligible) print line
    else if (source == "") prefix = prefix line "\n"

    if (index(line, "LF:") == 1) {
      line_fields++
      line_total = substr(line, 4)
    }
    if (index(line, "LH:") == 1) {
      hit_fields++
      line_hit = substr(line, 4)
    }
    if (line == "end_of_record") {
      flush_record()
      inside = eligible = 0
      prefix = source = line_total = line_hit = ""
      line_fields = hit_fields = 0
    }
  }
  END {
    if (inside) fail("unterminated LCOV record")
    if (kept == 0 || found == 0) fail("no instrumented production lines under " source_root)
    if (invalid) exit 1
    printf "[coverage] kept %d source records, %d/%d lines covered\n", kept, covered, found > "/dev/stderr"
  }
' "$BUILD_DIR/coverage.raw.info" \
  >"$FILTER_OUTPUT" 2>"$BUILD_DIR/coverage.filter.log"
mv "$FILTER_OUTPUT" "$BUILD_DIR/coverage.info"
FILTER_OUTPUT=""
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
