#!/usr/bin/env bash
# Verify shared-harness source preparation and exit propagation with tiny builds.
set -euo pipefail
root=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)
scratch=$(mktemp -d "${TMPDIR:-/tmp}/vpp-gate-runner-test.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
repo="$scratch/repo"
mkdir -p "$repo/benchmark" "$repo/scripts/quality" "$repo/gói/kiểm thử"
cp "$root/scripts/quality/run_benchmark_gate.sh" \
   "$root/scripts/quality/compare_benchmarks.sh" \
   "$root/scripts/quality/compare_benchmarks.cmake" "$repo/scripts/quality/"
printf '// collector fixture\n' > "$repo/scripts/quality/collect_benchmark_gate.vi"
printf '// comparator fixture\n' > "$repo/gói/kiểm thử/so sánh benchmark.vi"
cat > "$repo/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(benchmark_runner_fixture LANGUAGES CXX)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_library(fixture-runtime STATIC runtime.cpp)
add_subdirectory(benchmark)
CMAKE
printf 'const char *runtime_version() { return "baseline"; }\n' > "$repo/runtime.cpp"
printf 'message(FATAL_ERROR "Old harness must be replaced")\n' > "$repo/benchmark/CMakeLists.txt"
printf '#error old harness must not be compiled\n' > "$repo/benchmark/benchmark_baseline.cpp"
cat > "$repo/fixture-cli.sh" <<'SHELL'
#!/bin/sh
set -eu
case "$*" in
  *collect_benchmark_gate.vi)
    test "$("$VPP_GATE_BASELINE_BIN")" = 'baseline:harness-v3'
    test "$("$VPP_GATE_CANDIDATE_BIN")" = 'candidate:harness-v3'
    test "$VPP_GATE_HARNESS_COMMIT" = "$VPP_GATE_CANDIDATE_SHA"
    test "$VPP_GATE_BASELINE_SNAPSHOT_SHA" != "$VPP_GATE_BASELINE_REVISION_SHA"
    printf '{}\n' > "$VPP_GATE_OUTPUT/report.json"
    echo 'Shared harness and distinct runtime revisions verified'
    ;;
  *compare_benchmarks.vi)
    printf '{"status":"%s","cases":[]}\n' "$VPP_FIXTURE_DECISION"
    ;;
  *) exit 9 ;;
esac
SHELL
chmod +x "$repo/fixture-cli.sh"
export VPP_NATIVE_COMPILER="$repo/fixture-cli.sh"
git -C "$repo" init -q
git -C "$repo" add .
git -C "$repo" -c core.hooksPath=/dev/null -c user.name=Fixture \
    -c user.email=fixture@example.invalid commit -qm baseline
baseline=$(git -C "$repo" rev-parse HEAD)

printf 'const char *runtime_version() { return "candidate"; }\n' > "$repo/runtime.cpp"
cat > "$repo/benchmark/CMakeLists.txt" <<'CMAKE'
configure_file(benchmark_config.h.in benchmark_config.h @ONLY)
add_executable(vpp-benchmark-baseline benchmark_baseline.cpp telemetry.cpp)
target_include_directories(vpp-benchmark-baseline PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(vpp-benchmark-baseline PRIVATE fixture-runtime)
CMAKE
cat > "$repo/benchmark/benchmark_baseline.cpp" <<'CPP'
#include <iostream>
#include "telemetry.h"
const char *runtime_version();
int main() { std::cout << runtime_version() << ':' << telemetry_version(); }
CPP
printf '#define HARNESS_VERSION "harness-v3"\n' > "$repo/benchmark/benchmark_config.h.in"
printf 'const char *telemetry_version();\n' > "$repo/benchmark/telemetry.h"
cat > "$repo/benchmark/telemetry.cpp" <<'CPP'
#include "benchmark_config.h"
#include "telemetry.h"
const char *telemetry_version() { return HARNESS_VERSION; }
CPP
git -C "$repo" add .
git -C "$repo" -c core.hooksPath=/dev/null -c user.name=Fixture \
    -c user.email=fixture@example.invalid commit -qm candidate
candidate=$(git -C "$repo" rev-parse HEAD)
# Dirty working-tree files must never become the pinned harness.
printf '#error dirty harness must not be compiled\n' > "$repo/benchmark/benchmark_baseline.cpp"

for entry in PASS:0 FAIL:1 INCONCLUSIVE:2 INVALID:3; do
    decision=${entry%%:*}
    expected=${entry##*:}
    evidence="$scratch/$decision"
    status=0
    VPP_EXEC="$repo/fixture-cli.sh" VPP_FIXTURE_DECISION="$decision" \
        bash "$repo/scripts/quality/run_benchmark_gate.sh" \
        "$baseline" "$candidate" "$evidence" > "$scratch/$decision.log" 2>&1 || status=$?
    if [ "$status" -ne "$expected" ] || [ ! -f "$evidence/decision.json" ]; then
        cat "$scratch/$decision.log"
        echo "Benchmark runner $decision returned $status, expected $expected" >&2
        exit 1
    fi
    actual_hash=$(cmake -E sha256sum "$evidence/baseline-source.tar" | cut -d' ' -f1)
    grep -Fq "baseline_snapshot_sha256=$actual_hash" "$evidence/identities.txt"
    tar -xOf "$evidence/baseline-source.tar" ./runtime.cpp | grep -Fq '"baseline"'
    tar -xOf "$evidence/candidate-source.tar" ./runtime.cpp | grep -Fq '"candidate"'
    tar -xOf "$evidence/baseline-source.tar" ./benchmark/benchmark_baseline.cpp \
        > "$scratch/baseline-harness.cpp"
    tar -xOf "$evidence/candidate-source.tar" ./benchmark/benchmark_baseline.cpp \
        > "$scratch/candidate-harness.cpp"
    cmp "$scratch/baseline-harness.cpp" "$scratch/candidate-harness.cpp"
    echo "Benchmark runner $decision: shared harness, source identities and exit $status PASS"
done

status=0
VPP_EXEC="$repo/fixture-cli.sh" bash "$repo/scripts/quality/run_benchmark_gate.sh" \
    "$candidate" "$baseline" "$scratch/missing" > "$scratch/missing.log" 2>&1 || status=$?
test "$status" -eq 3
grep -Fq 'Candidate benchmark harness is missing benchmark/telemetry.cpp' "$scratch/missing.log"
status=0
bash "$repo/scripts/quality/run_benchmark_gate.sh" "$baseline" "$candidate" \
    "$scratch/PASS" > "$scratch/overwrite.log" 2>&1 || status=$?
test "$status" -eq 3
grep -Fq 'Refusing to overwrite benchmark evidence' "$scratch/overwrite.log"
echo 'Benchmark runner missing harness and evidence preservation PASS'
