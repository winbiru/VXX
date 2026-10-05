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
end-to-end VM execution, compiler stages, package/module resolution and GC cycle
collection. Stage setup/reset is kept outside the measured interval where practical.

For RC comparison, build baseline and candidate with the same harness, corpus, Release flags
and GC/JIT policy on the same machine. The benchmark executable prints stable machine-readable
`benchmark_meta=` and `benchmark=` lines. A repeated baseline/candidate comparator is still
missing from `scripts/quality/`, so those lines are currently raw input for a future gate.

The first recorded measurements live in `benchmark/BASELINE.md`.

```bash
cmake -S . -B build-benchmark -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=ON
cmake --build build-benchmark --target vpp-benchmark-baseline --parallel
./build-benchmark/bin/vpp-benchmark-baseline
```

A single benchmark run remains useful for local profiling, but RC performance decisions should
still use repeated same-machine samples with identical benchmark metadata and case sets.
