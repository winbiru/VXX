# Quality baseline

V++ keeps three repeatable quality gates in the repository: line coverage,
`clang-tidy`, and a small performance benchmark. They are intended to detect
regressions without adding third-party C++ benchmark dependencies.

## Coverage

Coverage instrumentation is enabled with `-DVPP_ENABLE_COVERAGE=ON` on GCC or
Clang toolchains compatible with gcov/LCOV. CI runs this gate on Ubuntu, executes
the full CTest suite, captures LCOV data, removes system headers,
C++ test code, V++ regression fixtures, examples, templates and generated build
files, then enforces a **45% line coverage** baseline.

```bash
./scripts/quality/run-coverage.sh
```

Set `VPP_COVERAGE_MINIMUM` to try a stricter threshold locally. Raise the
repository baseline only after the current CI report is comfortably above it.
The 12/09/2026 local source-based coverage cross-check measured 75.77%
(8,884/11,725 lines) with the same source/test/fixture exclusions; the CI gate
remains deliberately lower because GCC/LCOV and Apple LLVM coverage accounting
are not byte-for-byte identical.

## clang-tidy

The committed `.clang-tidy` file enables analyzer, bug-prone and performance
checks. To run it through CMake:

```bash
cmake -S . -B build-tidy -DBUILD_TESTS=OFF -DVPP_ENABLE_CLANG_TIDY=ON
cmake --build build-tidy --parallel
```

## Benchmark

The benchmark executable separates bytecode verification, preverified VM dispatch,
end-to-end VM execution, compiler stages, package resolution, JSON extraction and GC cycle
collection. Stage setup/reset is kept outside the measured interval where practical.

For RC comparison, build baseline and candidate with the same harness, corpus, Release flags
and GC/JIT policy on the same machine. `scripts/quality/benchmark-regression.py` alternates the
two binaries, uses 3 warm-ups and 40 measured samples by default, reports median/p95/MAD and
fails on a >10% median regression (15% for GC). Ten samples remain the minimum accepted for a
quick median check, but the GC p95 gate requires at least 40 samples; with fewer samples, a p95
threshold breach is `inconclusive` rather than a false regression. MAD/median above 5% is also
inconclusive rather than a pass.

The first recorded measurements live in `benchmark/BASELINE.md`.

```bash
cmake -S . -B build-benchmark -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=ON
cmake --build build-benchmark --target vpp-benchmark-baseline --parallel
./build-benchmark/bin/vpp-benchmark-baseline
```

Example same-machine comparison:

```bash
python3 scripts/quality/benchmark-regression.py \
  --baseline /path/to/baseline/vpp-benchmark-baseline \
  --candidate ./build-benchmark/bin/vpp-benchmark-baseline \
  --output benchmark/p0-report.json
```

The comparator requires identical benchmark metadata/case sets. A single benchmark run remains
useful for local profiling, but it is not enough evidence to close the RC performance gate.
