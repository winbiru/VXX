#!/usr/bin/env bash
# Usage: run_benchmark_gate.sh BASELINE_REF CANDIDATE_REF NEW_OUTPUT_DIR
# CMake/shell only prepare isolated revisions and run V++ orchestration.
set -euo pipefail

root=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)
if [ "$#" -ne 3 ]; then
  echo "usage: $0 BASELINE_REF CANDIDATE_REF NEW_OUTPUT_DIR" >&2
  exit 3
fi
baseline_ref=$1
candidate_ref=$2
output=$3
if [ -e "$output" ]; then
  echo "Refusing to overwrite benchmark evidence: $output" >&2
  exit 3
fi

baseline_sha=$(git -C "$root" rev-parse --verify "${baseline_ref}^{commit}")
candidate_sha=$(git -C "$root" rev-parse --verify "${candidate_ref}^{commit}")

# Both revisions must use identical workload code. Changes to the harness
# require a new baseline; otherwise a timing difference is not attributable
# to the candidate compiler/runtime.
for source in benchmark/benchmark_baseline.cpp benchmark/telemetry.cpp \
              benchmark/telemetry.h benchmark/benchmark_config.h.in benchmark/CMakeLists.txt; do
  before=$(git -C "$root" rev-parse "${baseline_sha}:${source}")
  after=$(git -C "$root" rev-parse "${candidate_sha}:${source}")
  if [ "$before" != "$after" ]; then
    echo "Benchmark harness changed between revisions ($source); select a compatible baseline" >&2
    exit 3
  fi
done

# The built-in CMake hash utility is supported on both macOS and Linux.
fingerprint=$(
  for source in benchmark/benchmark_baseline.cpp benchmark/telemetry.cpp \
                benchmark/telemetry.h benchmark/benchmark_config.h.in benchmark/CMakeLists.txt; do
    git -C "$root" rev-parse "${candidate_sha}:${source}"
  done | cmake -E sha256sum /dev/stdin | cut -d' ' -f1
)

mkdir -p "$output"
output=$(CDPATH= cd "$output" && pwd)
scratch=$(mktemp -d "${TMPDIR:-/tmp}/vpp-gate.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM

for side in baseline candidate; do
  revision=$baseline_sha
  if [ "$side" = candidate ]; then revision=$candidate_sha; fi
  mkdir -p "$scratch/$side"
  git -C "$root" archive "$revision" > "$scratch/$side.tar"
  tar -xf "$scratch/$side.tar" -C "$scratch/$side"
  archive_hash=$(cmake -E sha256sum "$scratch/$side.tar" | cut -d' ' -f1)
  if [ "$side" = baseline ]; then
    baseline_snapshot_hash=$archive_hash
  else
    candidate_snapshot_hash=$archive_hash
  fi
  cmake -S "$scratch/$side" -B "$scratch/$side-build" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=ON \
    -DVPP_ENABLE_COVERAGE=OFF > "$output/${side}-build.log" 2>&1
  cmake --build "$scratch/$side-build" --parallel 2 \
    --target vpp-benchmark-baseline >> "$output/${side}-build.log" 2>&1
done

baseline_bin="$scratch/baseline-build/bin/vpp-benchmark-baseline"
candidate_bin="$scratch/candidate-build/bin/vpp-benchmark-baseline"
baseline_hash=$(cmake -E sha256sum "$baseline_bin" | cut -d' ' -f1)
candidate_hash=$(cmake -E sha256sum "$candidate_bin" | cut -d' ' -f1)
cli=${VPP_EXEC:-"$root/build-ci-regression-fix/bin/vpp-cli"}
if [ ! -x "$cli" ]; then
  echo "V++ CLI missing or not executable: $cli" >&2
  exit 3
fi

export VPP_GATE_ROOT="$root" VPP_GATE_OUTPUT="$output"
export VPP_GATE_BASELINE_BIN="$baseline_bin" VPP_GATE_CANDIDATE_BIN="$candidate_bin"
export VPP_GATE_BASELINE_SHA="$baseline_sha" VPP_GATE_CANDIDATE_SHA="$candidate_sha"
export VPP_GATE_BASELINE_SNAPSHOT_SHA="$baseline_snapshot_hash"
export VPP_GATE_CANDIDATE_SNAPSHOT_SHA="$candidate_snapshot_hash"
export VPP_GATE_BASELINE_BINARY_SHA="$baseline_hash"
export VPP_GATE_CANDIDATE_BINARY_SHA="$candidate_hash"
export VPP_GATE_HARNESS_SHA="$fingerprint" VPP_GATE_HOST="$(uname -sm)"
export VPP_GATE_COLLECTOR_SHA="$(cmake -E sha256sum "$root/scripts/quality/collect_benchmark_gate.vi" | cut -d' ' -f1)"
export VPP_GATE_COMPARATOR_SHA="$(cmake -E sha256sum "$root/gói/kiểm thử/so sánh benchmark.vi" | cut -d' ' -f1)"
export VPP_GATE_CLI_SHA="$(cmake -E sha256sum "$cli" | cut -d' ' -f1)"
cpu_model=unknown
if command -v sysctl >/dev/null 2>&1; then
  cpu_model=$(sysctl -n machdep.cpu.brand_string 2>/dev/null || true)
fi
if [ -z "$cpu_model" ] && command -v system_profiler >/dev/null 2>&1; then
  cpu_model=$(system_profiler SPHardwareDataType 2>/dev/null | \
    awk -F: '/^[[:space:]]*Chip:/ {sub(/^[[:space:]]+/, "", $2); print $2; exit}')
fi
if [ -z "$cpu_model" ] || [ "$cpu_model" = unknown ]; then
  if [ -r /proc/cpuinfo ]; then
    cpu_model=$(awk -F: '/^model name|^Hardware/ {sub(/^[[:space:]]+/, "", $2); print $2; exit}' /proc/cpuinfo)
  fi
fi
export VPP_GATE_CPU="${cpu_model:-unknown}"
export VPP_GATE_OS="$(uname -sr)"
export VPP_ENABLE_JIT=0 VPP_ENABLE_MACHINE_JIT=0 VPP_VM_PROFILE=

printf 'baseline=%s\ncandidate=%s\nbaseline_snapshot_sha256=%s\ncandidate_snapshot_sha256=%s\nharness_sha256=%s\ncollector_sha256=%s\ncomparator_sha256=%s\ncli_sha256=%s\nos=%s\ncpu=%s\nbaseline_binary_sha256=%s\ncandidate_binary_sha256=%s\n' \
  "$baseline_sha" "$candidate_sha" "$baseline_snapshot_hash" "$candidate_snapshot_hash" \
  "$fingerprint" "$VPP_GATE_COLLECTOR_SHA" "$VPP_GATE_COMPARATOR_SHA" \
  "$VPP_GATE_CLI_SHA" "$VPP_GATE_OS" "$VPP_GATE_CPU" "$baseline_hash" "$candidate_hash" \
  > "$output/identities.txt"

"$cli" run --compiler=cpp "$root/scripts/quality/collect_benchmark_gate.vi" \
  | tee "$output/collection.log"

# The single V++ comparator determines PASS/FAIL/INCONCLUSIVE/INVALID and
# translates the result to distinct shell statuses 0/1/2/3 for CI.
VPP_EXEC="$cli" sh "$root/scripts/quality/compare_benchmarks.sh" "$output/report.json" \
  | tee "$output/decision.json"
