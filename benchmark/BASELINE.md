# Benchmark baseline and regression gate

## Reproducible same-machine comparison

```sh
python3 scripts/quality/test_benchmark_gate.py
python3 scripts/quality/run_benchmarks.py \
  --baseline cc675ca --candidate WORKTREE \
  --samples 40 --warmups 3 --target-ms 100 --jobs 4 \
  --output benchmark/reports/local-2026-10-24
python3 scripts/quality/compare_benchmarks.py benchmark/reports/local-2026-10-24/report.json
```

Use a **new** output directory for every run. `--baseline` accepts a Git revision;
the default is `HEAD^1`. Candidate defaults to a snapshot of `WORKTREE`, including
tracked modifications/deletions and untracked source files. Use `--candidate HEAD`
to measure a committed revision. The runner archives both revisions without changing
the index or working branch, builds them separately in Release, and overlays only
the exact same benchmark files on both revisions. No production code is backported.
An incompatible baseline API fails the build and cannot produce a green gate.
Sources and builds are retained in the printed temporary directory (or an explicitly
chosen new `--work-dir`). Source and binary SHA256, commit IDs, dirty patches, untracked
file list, CPU/OS/compiler/flags, controlled GC/JIT environment and commands are recorded.
The harness itself is copied into the report so its fingerprint can be audited.

`--cases vm_verify,gc_burst_collect_reuse` runs a subset; a subset PASS does **not**
close the full release performance gate. `--list` on the executable lists all 19 cases.
`--case NAME --iterations N` measures one case; JSON Lines contains build metadata and
one result. Running without arguments measures the complete catalog at default sizes.

The baseline pilot chooses iterations for approximately `--target-ms` per process;
the plan and budgets are saved **before** candidate measurement. Both sides then use
exactly the same iterations, with at least three unmeasured warm-up pairs and ten
independent measured process pairs per case. The first process alternates on successive
pairs (AB, BA, AB, BA). Default: 40 independent processes/version/case. Builds complete
before timing starts; sanitizer, coverage and profiler timing is rejected. Run on an
otherwise idle host. The tool never updates a baseline or relaxes a budget after seeing
candidate results.

`samples.jsonl` retains every warm-up and measured result. `report.json` combines the
raw observations and provenance; `comparison.json` and `summary.txt` show the decision.
The comparator can replay `report.json` without rebuilding or measuring anything.
Exit codes: **0 PASS, 1 FAIL, 2 INCONCLUSIVE, 3 INVALID**. Missing cases, observations,
checksums, GC telemetry, mismatched iterations/metadata, NaN and nonpositive timings
are invalid evidence. The CI `benchmark-gate` job uses the PR base/push-before SHA and
the candidate on the same runner and uploads all raw files even when the gate fails.

### Metrics and limits

| Metric | Measurement / default budget |
| --- | --- |
| Time | Median ns/iteration across independent processes; regression >10% fails |
| GC pause p95 | p95 of all individual measured collection pauses; regression >15% fails |
| Peak RSS | Median of per-process lifetime peaks from `getrusage` (macOS bytes, Linux KiB converted to bytes) or Windows `PeakWorkingSetSize`; regression >15% fails |
| Allocations | Successful C++ `new`/`new[]` requests and requested bytes per burst/collect/reuse cycle; regression >15% fails |
| Noise | MAD/median >5% for timings, RSS, allocations or independent-process GC p95 makes the case INCONCLUSIVE |

Median, p95, MAD, min/max and sample count are reported. Percentiles use linear
interpolation at `(n-1)*p`. GC reports retain every pause and the dispersion of
per-process p95 as well as pooled p95. Fewer than 40 independent processes makes
the GC case INCONCLUSIVE, even if it contains many individual pauses. INCONCLUSIVE
and INVALID both block CI; neither is counted as PASS.

Allocation interception is linked **only** into the benchmark executable, counts on
the measuring thread and excludes C `malloc`, OS mappings and allocator bookkeeping.
The same interceptor is present in both builds. These are host C++ allocation requests,
not a claim about all process allocations. `managed_allocations` separately records
1024 actual runtime objects per burst, verified against sweep/root counts. GC-specific
allocation count/bytes exclude the burst and reuse. RSS includes executable startup,
shared libraries and harness fixtures; it is not retained/live managed heap size.
One case per process prevents earlier measured cases from contaminating its RSS peak.
This benchmark does not claim a production-runtime profiler or a whole-system RSS view.

### Workloads and timer boundaries

- `vm_verify`: constructs verification context and verifies prepared bytecode.
- `vm_dispatch_preverified`: resets outside timer, executes 1601 instructions through
  interpreter, checks PC/stack/result. Includes interpreter end-of-program GC; excludes
  VM construction and bytecode verification. JIT is disabled.
- `vm_end_to_end`: VM construction, verification, dispatch, final GC and destruction.
- Function calls and module initializers: 10k direct/consumed calls and 32 initializers
  sharing 622 functions; call results validated. Module VM construction is outside timer.
- Compiler: lexer, normalization, parser, semantics, IR, optimizer, codegen and full
  pipeline on the same 24-function source. Fresh mutable optimizer/codegen fixtures
  are reset outside timer. Outputs must be nonempty and consistent on every iteration.
- Package/module graph: resolves `dữ liệu` in each revision's stdlib using an isolated
  source directory and that revision's `VPP_HOME`; imported library implementation is
  part of the product being compared, not an external fixed library.
- `gc_collect_cycles`: prepares 128 self-cycles outside timer; times collection only.
- `gc_burst_collect_reuse`: times 1024 self-cyclic list allocations, collection and
  refilling the reused stack. A live root with value 42 survives; every cycle is swept.
  Periodic-style collection must preserve stack capacity with zero trim/resize.
- `gc_burst_collect_trim_reuse`: identical workload with explicit capacity trim,
  recording resulting trims/resizes and their allocation/time costs.

This harness measures the explicit VM collection path. It does not yet measure
HTTP/JSON over sockets, GC under every interpreter scheduling condition, variable-map
rehash events, caller/callee unwind, or installed-artifact correctness. Those remain
separate release evidence. A benchmark PASS alone does not close all P0/1.0 gates.

## Local evidence, 24/10/2026

Full run: [`local-2026-10-24/summary.txt`](reports/local-2026-10-24/summary.txt),
[`raw report`](reports/local-2026-10-24/report.json),
[`comparator decision`](reports/local-2026-10-24/comparison.json).
Baseline `cc675cad8fc8` vs candidate `b39933e2b2a` plus the recorded working-tree
benchmark patch, on Apple M3 Pro / macOS ARM64, AppleClang 21, Release `-O3 -DNDEBUG`.
Three warm-up and 40 measured process pairs for each of all 19 cases:
**16 PASS, 3 INCONCLUSIVE, 0 FAIL**. The overall gate remains **INCONCLUSIVE**.
Noise exceeds 5% on module initialization, package resolution and module graph;
these cases require a new run on an idle host. Budgets were not changed.

| GC workload | Baseline | Candidate |
| --- | ---: | ---: |
| Reuse: collection pause p95 | 58.750 µs | 59.583 µs |
| Reuse: cycle median time | 198.248 µs | 199.108 µs |
| Reuse: peak RSS (median process peak) | 8.88 MiB | 8.89 MiB |
| Explicit trim: collection pause p95 | 60.333 µs | 59.917 µs |
| Explicit trim: cycle median time | 202.250 µs | 201.016 µs |

Both GC cases PASS. Every burst allocates/sweeps 1024 runtime objects while
preserving its live root. Periodic-style GC observes zero stack resize/trim;
the explicit-trim case records one trim and five stack growths per cycle.
Allocation request count/bytes, raw pauses and dispersion are in `report.json`.
This is evidence for the benchmark implementation, not approval of RC or Stable.

## Historical observations

Baseline recorded on 12/09/2026 with a Release build on macOS 26.6.2 arm64,
Apple Clang 21.0.0. These numbers are reference observations only; compare runs
on the same machine and build mode before treating a change as a regression.

| Benchmark | Iterations | Total | Per iteration |
| --- | ---: | ---: | ---: |
| VM dispatch | 250 | 3.396 ms | 13,585.7 ns |
| Lexer | 5,000 | 29.519 ms | 5,903.8 ns |
| Compiler pipeline | 250 | 9.999 ms | 39,995.0 ns |
| Native HTTP helpers (historical; helper removed) | 100,000 | 297.695 ms | 2,976.9 ns |

The HTTP row is retained only as historical evidence from the 12/09/2026 harness; the
corresponding native helper no longer exists. The current benchmark executable covers
VM verification/dispatch/end-to-end, compiler stages, package/module resolution and GC
collection, and prints machine-readable `benchmark_meta=` / `benchmark=` records.
