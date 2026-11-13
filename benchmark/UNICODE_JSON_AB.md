# Native V++ Unicode/JSON A/B benchmark

Use the same self-hosted native compiler to build the current working tree and
a detached, temporary worktree at the baseline Git revision. The benchmark
program is identical in both builds; the module and host ABI sources are taken
from their respective trees. The script checks JSON source hashes, binary
hashes, outputs and checksums, then reports median and MAD across randomized
samples. It removes the temporary baseline worktree automatically.

```bash
python3 scripts/quality/benchmark_unicode_json_ab.py \
  --native-compiler build/native-compiler-json-inline/vpp-native \
  --baseline-ref HEAD \
  --output-dir build-benchmark-unicode-json-ab \
  --runs 11 --iterations 16
```

The JSON report is written to `build-benchmark-unicode-json-ab/unicode-json-ab.json`.
Results are **native AOT** in-process timing on the machine executing the
benchmark. The `unicode_replace` workloads exercise string replacement;
`json_escape` workloads exercise JSON string escaping, **not parsing**.

Cross-platform V++ **C++ VM** language comparisons are a separate benchmark:

```bash
python3 scripts/quality/run_portable_cross_os_bench.py \
  --vpp-cli build/bin/vpp-cli --output-dir build-benchmark-portable \
  --runs 5 --target-ms 30
```

The GitHub Actions workflow `benchmark-cross-os.yml` runs that portable suite
on three native hosted runners. Compare each language against V++ **within** a
runner; do not compare raw runtimes across heterogeneous hardware.

## JSON numeric parser hot path (native AOT)

`benchmark/json_serialize_ab.vi` separately exercises JSON parsing and JSON
serialization. Build the **same benchmark file** using the same self-hosted
native compiler before/after a parser change, and run each binary with
`BENCH_CASE=parse_nested` and `BENCH_ITERATIONS=12`. The benchmark constructs
its nested Unicode JSON input before timing and checks the output on every run.
`serialize_nested` and `serialize_long` are control cases for regressions.

On macOS ARM64, 13 randomized before/after rounds (same compiler and program,
in-process elapsed time without compilation or startup) for the one-digit i32
fast path in `json đọc giá trị`/`json đọc số` measured:

| Case | Before (median ms/op) | After (median ms/op) | Speedup |
| --- | ---: | ---: | ---: |
| `parse_nested` | 4.8454 | 3.9258 | 1.23× |
| `serialize_nested` | 3.5897 | 3.5973 | 1.00× |
| `serialize_long` | 0.7468 | 0.7406 | 1.01× |

Both variants produced matching checksums for all 13 rounds. Results and the
binary hashes are recorded in the locally generated
`build-benchmark-2026-11-08-unicode-json/json-parser-hotpath-ab.json`. An
alternative serializer built with a segment list was rejected because it was
slower, as was an additional two-digit numeric shortcut.

## JSON serializer: numeric arrays and very large arrays (2026-11-08)

The serializer now appends integer array items directly to the destination
string (`result + item`). That removes a recursive JSON-dispatch call and an
intermediate integer string per element. For arrays with at least 512 items,
it serializes subranges of at most 32 items and joins those subranges in a
balanced tree without allocating a list of fragments. The previous
fragment-list serializer was **reverted** after showing a strong regression.

On macOS ARM64, 13 randomized paired rounds of 24 operations each, with the
same native compiler and benchmark source for each pair, measured:

| Case | Before (ms/op) | After (ms/op) | Speedup |
| --- | ---: | ---: | ---: |
| `parse_nested` (integer-array fast path) | 3.1321 | 3.1181 | 1.00× |
| `serialize_nested` (integer-array fast path) | 3.5341 | 3.1847 | 1.11× |
| `serialize_long` (384 integers, integer-array fast path) | 0.7450 | 0.3089 | **2.41×** |
| `serialize_very_long` (4096 integers, balanced subranges added) | 6.0014 | 3.3425 | **1.80×** |

The 4096-integer comparison uses the already-optimized integer-array variant
as its baseline; its speedup is incremental to the earlier change. The
corresponding 13-round measurements for `parse_nested`, `serialize_nested`,
and `serialize_long` stayed within roughly 1% of their baseline when the
balanced-subrange branch was introduced. Both variants had identical output
checksums in every measurement. Measurements and binary hashes are in
`build-benchmark-2026-11-08-unicode-json/array-fast-ab/array-int-direct-comparison.json`
and `.../chunked-very-long-comparison.json`.

The `serialize_very_long` case is built into `benchmark/json_serialize_ab.vi`.
To rerun a compiled benchmark, set `BENCH_CASE` and `BENCH_ITERATIONS` in the
environment. These are **native AOT** in-process numbers from one macOS
machine; they are not three-OS measurements. JSON regression tests include
round-trip checks at the 512-item transition and Unicode/escaped text.

## JSON numeric-array parser: multi-digit fast paths (2026-11-08)

The parser now processes consecutive integer array items in V++ without
calling the general JSON value/number parser for each item. It handles
common positive 2–5 digit integers using bounded, unrolled digit arithmetic
and uses a separate checked path for signed 32-bit integers (including
`-2147483648`). The existing single-digit shortcut remains. Decimals,
exponents, integers beyond i32 and malformed JSON fall back to the original
general parser. There are no new ASM opcodes or C++ dependencies.

Eleven randomized paired rounds on macOS ARM64, using the same native AOT
compiler and identical benchmark source for both versions, yielded these
medians. Each run uses 24 timed operations, except `parse_huge` with 8.
Input construction, process startup and correctness preflight are excluded.

| Case | Before (ms/op) | After (ms/op) | Speedup |
| --- | ---: | ---: | ---: |
| `parse_long` (384 integers) | 2.6045 | 0.6884 | **3.78×** |
| `parse_very_long` (4,096 integers) | 31.4036 | 8.6644 | **3.62×** |
| `parse_huge` (16,384 integers) | 145.4819 | 36.7521 | **3.96×** |
| `parse_nested` (control) | 3.2053 | 3.1992 | 1.00× |
| `serialize_nested` (control) | 3.2993 | 3.2670 | 1.01× |
| `serialize_very_long` (control) | 3.3088 | 3.2987 | 1.00× |

The measurements, executable SHA-256 hashes, and individual samples are in
`build-benchmark-2026-11-08-unicode-json/array-fast-ab/parse-multidigit-final-ab.json`.
The benchmark checks exact serialized array content before timing. Its current
source also verifies each parsed integer against its original index before
timing; numeric checksums inside the timed loop only guard against a missing
operation, so the preflight and regression tests remain essential.

All six JSON regression fixtures passed after the parser change, including
the new 511/512/513-element serialization boundaries, signed i32 limits,
Unicode within mixed arrays, floats, exponents, and malformed literals.
An experimental byte-at-a-time JSON output buffer was discarded because it
was slower than balanced string concatenation (roughly 15.5 vs 3.3 ms per
4,096-integer serialization in a single 24-operation probe).

## Unicode and JSON native runtime study — 2026-11-12

Baseline: `35aa30e` (`Feature/benchmark_refactor_module`). Platform: macOS ARM64. All times below measure application execution, excluding compilation, startup and input construction. No external C/C++ runtime dependency was introduced.

### Changes

- Runtime: cache UTF-8 byte metadata, count four ASCII bytes at once, and skip unmatched words when a replacement needle starts with ASCII. Keep decoded boundaries, literal matching, malformed-byte behavior and bounded reads.
- JSON: validate UTF-8 directly, keep the integer-array cursor local in the existing fast paths, and store object entries directly.
- Benchmark: construct the numeric input with balanced concatenation. The original sequential concatenation exhausted the managed heap before parsing at 12,288 and 16,384 integers; identical corrected input is used on both sides. The parser successfully handles 16,384 integers.

### Controlled measurements

Seven randomized rounds (seed 17 for workload data; fixed randomized execution order), identical workload sources, profiling disabled. Median µs/op; MAD in parentheses. The six variants isolate JSON source changes from the pure V++ runtime changes.

| Variant | Unicode replace | JSON parse |
| --- | ---: | ---: |
| baseline-vpp | 60.150 (1.344) | 496.270 (1.463) |
| json-opt-vpp | 59.372 (0.550) | 486.961 (2.156) |
| runtime-opt-vpp | 56.618 (0.603) | 495.111 (4.842) |
| combined-opt-vpp | 56.907 (0.808) | 484.480 (2.691) |
| baseline | 5.993 (0.115) | 59.508 (0.162) |
| json-opt-cpp | 6.060 (0.080) | 54.355 (0.510) |

Combined pure V++ time decreased 5.39% for Unicode and 2.38% for JSON; runtime-only Unicode decreased 5.87%. JSON source changes reduced C++ runtime JSON time by 8.66%. An earlier independent run found approximately 4.3% and 2.0% combined improvements on pure V++. The remaining gap to the C++ runtime is large.

Controls (ns/op, seven rounds):

| Workload | Before | After |
| --- | ---: | ---: |
| arithmetic | 2.027 | 2.035 |
| branch_loop | 246.233 | 247.475 |
| fibonacci | 88.058 | 86.707 |
| list_128 | 163.364 | 163.300 |

No material regression was identified on those controls.

#### Large JSON

Five randomized rounds per workload, two warmup batches, identical balanced input preparation. Median ms/op (MAD); the final column is the percentage reduction in pure V++ median time (negative means slower). The script also checks expected array counts and canonical serialization lengths.

| Workload | V++ before | V++ after | C++ before | C++ after | V++ reduction | Result per op |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| parse_nested | 20.034 (1.024) | 18.797 (0.389) | 2.617 (0.007) | 2.235 (0.019) | +6.17% | 64 |
| parse_long | 4.519 (0.049) | 4.378 (0.038) | 0.525 (0.011) | 0.478 (0.005) | +3.11% | 384 |
| parse_very_long | 63.027 (0.295) | 61.920 (0.067) | 6.484 (0.093) | 5.889 (0.061) | +1.76% | 4096 |
| parse_huge | 308.552 (7.114) | 295.833 (1.949) | 27.702 (0.457) | 24.874 (0.097) | +4.12% | 16384 |
| serialize_nested | 24.719 (0.077) | 24.901 (0.253) | 2.879 (0.038) | 2.900 (0.040) | -0.74% | 3043 |
| serialize_long | 5.108 (0.035) | 5.007 (0.039) | 0.300 (0.009) | 0.293 (0.001) | +1.97% | 1427 |
| serialize_very_long | 41.990 (0.565) | 43.435 (1.206) | 3.185 (0.045) | 3.178 (0.027) | -3.44% | 19371 |

Parsing improved across all sizes; an earlier five-round run after the setup fix also showed improvements (3.46%, 4.00%, 2.98%, 5.30% for the four parsing cases). Serialization is a control rather than a target here: the final 4096-element pure V++ serialization median was 3.44% slower, with noisy samples; the preceding five-round run showed a 0.79% slowdown. Do not claim a serialization improvement or a proven absence of regression.

### Reproduction

Use a single verified native compiler and runtime compiler for all variants. The following commands assume the source changes in this report are present; use an empty output directory. The baseline tree is a Git archive, not a modified checkout. The balanced benchmark setup is copied to the baseline to maintain identical input.

```bash
root="$PWD"
d="$root/build-benchmark-unicode-json-2026-11-12"
compiler="$root/build/native-compiler/vpp-native"
stage1="$root/build/native-vpp-runtime/vpp-i32"
mkdir -p "$d/baseline-tree" "$d/runtime-base" "$d/runtime-opt"
git archive 35aa30e | tar -x -C "$d/baseline-tree"
cp benchmark/json_serialize_ab.vi "$d/baseline-tree/benchmark/json_serialize_ab.vi"
base="$d/baseline-tree"
VPP_HOME="$base" "$stage1" runtime-object "$base/gói/máy ảo/runtime AOT.vi" -o "$d/runtime-base/runtime.o"
VPP_HOME="$root" "$stage1" runtime-object "$root/gói/máy ảo/runtime AOT.vi" -o "$d/runtime-opt/runtime.o"
build_vpp() {
  VPP_HOME="$1" VPP_AOT_RUNTIME=vpp VPP_AOT_VPP_OBJECT="$d/$2/runtime.o" \
    "$compiler" build "$1/$3" -o "$d/$4"
}
build_cpp() {
  VPP_HOME="$1" VPP_AOT_RUNTIME=cpp "$compiler" build "$1/$2" -o "$d/$3"
}
small=benchmark/languages/workloads.vi
large=benchmark/json_serialize_ab.vi
build_vpp "$base" runtime-base "$small" baseline-vpp
build_vpp "$root" runtime-base "$small" json-opt-vpp
build_vpp "$base" runtime-opt "$small" runtime-opt-vpp
build_vpp "$root" runtime-opt "$small" combined-opt-vpp
build_cpp "$base" "$small" baseline
build_cpp "$root" "$small" json-opt-cpp
build_vpp "$base" runtime-base "$large" json-large-base-vpp
build_vpp "$root" runtime-opt "$large" json-large-opt-vpp
build_cpp "$base" "$large" json-large-base-cpp
build_cpp "$root" "$large" json-large-opt-cpp
python3 scripts/quality/benchmark_unicode_json_native.py "$d" --runs 7
python3 scripts/quality/benchmark_unicode_json_native.py "$d" --large --runs 5
```

Reports `native-ab.json` and `native-large-ab.json` contain raw samples, checks, source/runtime/binary SHA-256 hashes. Subset runs use distinct filenames. The recorded hashes were checked against the actual files after both runs. The harness measures prebuilt binaries; rebuild if sources change.

| Input | Baseline SHA-256 | Current SHA-256 |
| --- | --- | --- |
| `benchmark/languages/workloads.vi` | `21cb4bb0ae8fb9c272b616b33c13366ec8658e1162c86e5df9ce9e9a8c2d68b1` | `21cb4bb0ae8fb9c272b616b33c13366ec8658e1162c86e5df9ce9e9a8c2d68b1` |
| `gói/dữ liệu/json/json.vi` | `496be95ee1f29be970905d3bd53826e1a3aafd69a5d91e69542de6460938ddbf` | `e4853e92ab112594cfea05edba8210a982ae4f994b17a3c1e420caa3da8d2579` |
| `gói/máy ảo/host ABI.vi` | `a90a019dd014e9f950252c1bdca6b1f377efbf53159c64046d64bfef43fe3620` | `b7a34cb6315aac0f71a645a923a35c7b1962559a912e8663fdf9f3365f432699` |
| `benchmark/json_serialize_ab.vi` | `e9eab7aefa754c0fbff23ed52949d71bdf177a5eae56d2db084e04ebb14b2066` | `e9eab7aefa754c0fbff23ed52949d71bdf177a5eae56d2db084e04ebb14b2066` |
| Runtime object | `277c6be90c8d1b579e19d5da215297713849dbee18cbceb0257231aac1c0a018` | `81528d1642a58cdb6a8ea618c9d1960489aa5680bcd4593202685156f2babe0a` |

### Correctness and remaining work

Five fixtures (string foundation, JSON UTF-8, parser, safety and controls) matched expected output on interpreter, machine JIT, AOT/C++ and AOT/V++ (20 combinations). The exhaustive ASCII replacement test covers all 128 bytes, four lanes, invalid-byte padding, match/no-match and NUL. New tests also cover malformed spans joining into a valid code point, UTF-8 boundaries, trailing invalid input, numeric-path transitions and malformed JSON. Dynamic AOT parity, GC, symbols and dependency audit passed; the pure AOT binary links only libSystem. The compiler was not installed or replaced.

Built-in runtime profiling points to allocation, generic binary operations and calls (overlapping counters, not exclusive CPU percentages). External macOS sampling was unavailable due to sandbox permissions. The remaining bottleneck is boxed values and byte-list indexing: JSON converts the input into a mutable list of integer references, even though byte values are cached. Next experiments should isolate cached byte-table access, a dense internal read-only byte view (retain public mutable list semantics), unboxed cursor arithmetic and compiler inlining of hot runtime calls. Investigate bulk copy, string builders and allocation/GC separately. No measured claim is made for these proposed changes.

Full macOS ARM64 executable self-host gate passed: stages 1 and 2 are byte-identical (SHA-256 `eb003d6c93653eaa5d16bce942beaca34019a10829340ff85b0ead6e6e9e8748`), signed, valid Mach-O layout, Unicode path/runtime probes and malformed input regressions all passed. No external compiler/assembler/linker ran during the two stages. Output: `build-benchmark-unicode-json-2026-11-12/selfhost-executable/`.

Only macOS ARM64 was measured. Linux/Windows require separate verification.

### Follow-up: byte representation and runtime architecture

An isolated follow-up experiment hoisted the managed small-integer cache pointer out of `vm_byte_array` and looked up cached byte references directly, retaining lazy initialization and the existing mutable list representation. The experiment used a copy of the current runtime source, not the working source. Seven randomized rounds for small cases and five for large cases used the same application source and checked matching results and checksums.

| Case | Current median | Experiment median | Time reduction |
| --- | ---: | ---: | ---: |
| json_parse | 477.373 us | 476.262 us | 0.23% |
| unicode_replace | 55.048 us | 54.687 us | 0.66% |
| parse_nested | 19.139 ms | 18.956 ms | 0.96% |
| parse_very_long | 61.466 ms | 60.175 ms | 2.10% |
| parse_huge | 298.122 ms | 291.587 ms | 2.19% |
| serialize_very_long | 41.448 ms | 41.087 ms | 0.87% |

The result does not justify changing production runtime code: the small JSON effect is within variation and the large-array effect is only around 2%. The experiment remains isolated in `build-benchmark-unicode-json-2026-11-12/byte-cache-experiment/` (including `comparison.json` and its binary hashes). It does not establish a speedup from eliminating byte-list materialization.

**Historical recommended experiments (before CP3–CP8):**

1. Implement a private, immutable UTF-8 byte view retaining its source string as a GC root. Keep `chuỗi thành byte máy ảo` returning a mutable list; preserve its size limit for existing callers. Implement equivalent bounds checks and malformed-byte handling on all backends. Change JSON source scans to use the view, with an independent A/B against current code. Measure input conversion, scanning and result materialization separately.
2. Specialize native byte loads and unboxed i32 cursor updates. Existing `aot_index_slot`, `aot_index_compare_integer` and related fused helpers already optimize list accesses; extend their lowering for the byte view rather than adding competing generic paths. Keep the dynamic fallback and prove bounds and GC lifetime.
3. Add bulk primitives for byte scanning, copying and UTF-8 validation (scalar V++ reference, ARM64 NEON and x86-64 SIMD via ASM). Use SWAR for short inputs and vector blocks for longer inputs; handle tails without out-of-bounds reads. Preserve raw invalid UTF-8 behavior in string operations; JSON must reject invalid input everywhere.
4. Evaluate a two-stage structural JSON scanner and a compact token/offset buffer, with full eager materialization into the existing object/list API. Avoid comparing lazy parsing against the current fully materialized parser. Keep a short-document scalar path and check exact error behavior.
5. Prototype a managed byte builder for serialization and replacement with checked capacity growth and bulk copies. Consider immediate tagged integers and packed integer arrays only as separate ABI/GC projects, with overflow fallbacks and bootstrap compatibility.

Research references: [simdjson paper](https://arxiv.org/abs/1902.08318), [simdjson two-stage parsing](https://simdjson.github.io/simdjson/md_doc_2parse__many.html), [UTF-8 lookup algorithm](https://lemire.me/en/publication/arxiv2010.03090/), [Rust byte slices](https://doc.rust-lang.org/std/primitive.str.html), [V8 JSON serialization](https://v8.dev/blog/json-stringify), [V8 tagged integers](https://v8.dev/blog/pointer-compression), [V8 packed elements](https://v8.dev/blog/elements-kinds). These are design references, not measured V++ speedups.

Acceptance: identical fixture outputs on interpreter/JIT/AOT C++/AOT V++, malformed UTF-8 and JSON corpus, NUL and tail bounds, forced GC stress, randomized before/after benchmarks across document sizes and Unicode mixes, control workloads and self-host convergence. Linux/Windows require their own measurements. No new C/C++ runtime dependency.

### Integrated CP3 + CP5d — verified incremental A/B (macOS ARM64)

Comparison against the pre-roadmap optimized runtime and JSON module, not the original HEAD. Prior sections document historical measurements. Each case used randomized paired runs and matching checksums; escape/object workloads also compared exact serialized contents.

| Case | Before | Integrated | Time change |
| --- | ---: | ---: | ---: |
| json_parse | 492.188 us | 494.500 us | +0.47% |
| unicode_replace | 57.429 us | 56.035 us | -2.43% |
| arithmetic | 2.090 ns | 2.116 ns | +1.25% |
| branch_loop | 251.614 ns | 251.734 ns | +0.05% |
| fibonacci | 89.587 ns | 89.417 ns | -0.19% |
| list_128 | 166.962 ns | 167.572 ns | +0.37% |
| parse_nested | 19.804 ms | 19.755 ms | -0.25% |
| parse_long | 4.620 ms | 4.608 ms | -0.25% |
| parse_very_long | 63.098 ms | 63.499 ms | +0.64% |
| parse_huge | 310.646 ms | 311.834 ms | +0.38% |
| serialize_nested | 26.286 ms | 26.057 ms | -0.87% |
| serialize_long | 5.277 ms | 5.275 ms | -0.05% |
| serialize_very_long | 42.651 ms | 42.745 ms | +0.22% |
| escape | 46.168 ms | 23.274 ms | -49.59% |
| object | 568.952 ms | 515.359 ms | -9.42% |
| ascii_length | 27.801 us | 19.926 us | -28.33% |
| long_replace | 145.445 us | 53.734 us | -63.06% |

CP3 implements a scalar four-word ASCII fast path and a UTF-8 replacement boundary shortcut; it does not implement SIMD. CP5d implements lazy balanced escape fragments and balanced large-object serialization; it does not implement a raw-byte builder. CP1/CP2/CP4/CP6 experiments were rejected for regressions or incorrect behavior. At this checkpoint, immutable byte views, cursor specialization, SIMD, managed builders and packed arrays remained open. CP8 below implements bulk copying and SIMD ASCII-prefix scanning.

Fresh verification: 20/20 fixture/backend combinations (interpreter, JIT, AOT C++, AOT V++), 37,711 exact UTF-8 differential lines, dynamic AOT parity including GC and dependency audit, and full executable self-host gate passed. The two compiler stages are byte-identical, SHA-256 `eb003d6c93653eaa5d16bce942beaca34019a10829340ff85b0ead6e6e9e8748`; no external build tools during stages. Linux/Windows not tested.

Raw reports: `build-benchmark-unicode-json-2026-11-12/integration-cp5d/comparison.json`, `extra_comparison.json`, `long_comparison.json`; runtime object SHA-256 `a612f3262b9eb7b39fbb8f8c966f3b0f2a47d827aae1650b70605318e5c3fd71`. Re-run with `python3 build-benchmark-unicode-json-2026-11-12/integration-cp5d/compare.py`, then `extra_compare.py` and `long_compare.py` in sequence, after building current binaries.

Source SHA-256:
- `gói/máy ảo/host ABI.vi`: `ac0ebe5c1080c00af6786c434ab2cb841e5fa0db90fb14222339777b4e7a9b71`
- `gói/dữ liệu/json/json.vi`: `f1349022c16a79b9e6b78a0f31c3a828a0c9a8613d72cabf285d570d69e93aa6`
- `benchmark/unicode_long_ab.vi`: `e70e27b0eaa4200477a4fe1c662a578de8ba305df93b41f6b572e4890e9b0822`
- `benchmark/json_large_escape_ab.vi`: `6066f180c9a14bbbb4d20ae299d5ab73fe4e445b9890fa146a97e1323cf2962c`

### Integrated CP7 — string-key map (macOS ARM64)

CP7 keeps sorted reference arrays for homogeneous string-key maps, uses cached raw-string pointers and four-byte equality skips in comparisons, and reuses the binary-search insertion position. Allocation failure in the specialized insertion path preserves the previous map. Numeric and mixed-key behavior remains unchanged.

Five paired rounds per 512-key batch against CP3+CP5d, with matching checks and final values:

| Case | Before (ms) | CP7 (ms) | Speedup |
| --- | ---: | ---: | ---: |
| insert | 70.712375 | 4.039750 | 17.50x |
| lookup | 3.989070 | 1.651172 | 2.42x |
| update | 7.681156 | 2.940031 | 2.61x |
| delete | 128.429125 | 37.257625 | 3.45x |

Fresh general controls and JSON measurements against the same incremental baseline:

| Case | Before | CP7 | Time change |
| --- | ---: | ---: | ---: |
| json_parse | 497.809 us | 503.754 us | +1.19% |
| unicode_replace | 55.835 us | 47.387 us | -15.13% |
| arithmetic | 2.118 ns | 2.116 ns | -0.10% |
| branch_loop | 251.019 ns | 250.887 ns | -0.05% |
| fibonacci | 92.404 ns | 90.269 ns | -2.31% |
| list_128 | 164.158 ns | 163.696 ns | -0.28% |
| parse_nested | 19.714 ms | 19.030 ms | -3.47% |
| parse_long | 4.598 ms | 4.568 ms | -0.67% |
| parse_very_long | 63.456 ms | 62.798 ms | -1.04% |
| parse_huge | 302.686 ms | 311.290 ms | +2.84% |
| serialize_nested | 26.426 ms | 25.279 ms | -4.34% |
| serialize_long | 5.421 ms | 5.307 ms | -2.10% |
| serialize_very_long | 43.106 ms | 42.209 ms | -2.08% |
| escape | 24.377 ms | 23.931 ms | -1.83% |
| object | 518.943 ms | 509.282 ms | -1.86% |

General workloads are approximately stable; the measured Unicode replacement improvement is not causally attributed to the map change. All 36 fixture/backend combinations, dynamic parity, exact map differential and full macOS executable self-host passed. Standalone exact-dependency allocation test exited 0 (forced heap exhaustion, recovery, numeric/mixed keys and GC). The previous AOT V++ `xóa khóa` return-value inconsistency persists: successful deletion returns 1 instead of the removed value, while mutation works. The test checks mutation; API inconsistency is separate from this optimization.

Runtime SHA-256: `54f7c21a9a93d385c4e522983d68c02bdb91c8da95705e57f4c350d8ee5858b6`. Compiler stages byte-identical: `eb003d6c93653eaa5d16bce942beaca34019a10829340ff85b0ead6e6e9e8748`. Raw reports and verification logs: `build-benchmark-unicode-json-2026-11-12/integration-cp7/`. Linux/Windows native execution not verified.


### Integrated CP8 — overlap-safe bulk copies and SIMD ASCII prefix (macOS ARM64)

CP8 introduces two internal native primitives (`vpp_native_memory_copy` and
`vpp_native_memory_ascii_prefix`, native core IDs 82/83) implemented in ASM
for supported targets. String slicing/concatenation and byte copies use bulk
memory copy; vector growth, list removal and sorted map insertion use
reference copies with memmove semantics. Bounds checks precede copies;
overlapping ranges, zero length, and both directions are supported. Existing
GC reference storage and ownership remain unchanged. ASCII-prefix scanning
uses SIMD with bounded scalar tails; mixed/malformed UTF-8 follows the
existing scalar semantics. No new C/C++ production runtime dependency.

Eight configurations (CP7 control and seven candidates) were compared over
21 cases, five randomized paired rounds, with matching outputs/checksums.
The combined string/reference copy and ASCII-prefix candidate was selected.
Incremental CP7 → CP8 measurements from the isolated experiment:

| Case | Before | After | Speedup | Time change |
| --- | ---: | ---: | ---: | ---: |
| json_parse | 490.160 us | 483.973 us | 1.01x | -1.26% |
| unicode_replace | 47.699 us | 45.663 us | 1.04x | -4.27% |
| arithmetic | 2.066 ns | 2.080 ns | 0.99x | +0.68% |
| branch_loop | 250.645 ns | 247.950 ns | 1.01x | -1.08% |
| fibonacci | 88.862 ns | 88.860 ns | 1.00x | -0.00% |
| list_128 | 166.256 ns | 163.399 ns | 1.02x | -1.72% |
| parse_nested | 19.288 ms | 18.913 ms | 1.02x | -1.94% |
| parse_long | 4.590 ms | 4.675 ms | 0.98x | +1.85% |
| parse_very_long | 62.203 ms | 62.097 ms | 1.00x | -0.17% |
| parse_huge | 306.220 ms | 298.293 ms | 1.03x | -2.59% |
| serialize_nested | 24.890 ms | 24.370 ms | 1.02x | -2.09% |
| serialize_long | 5.138 ms | 4.352 ms | 1.18x | -15.29% |
| serialize_very_long | 42.102 ms | 40.717 ms | 1.03x | -3.29% |
| escape | 23.264 ms | 22.532 ms | 1.03x | -3.14% |
| object | 503.502 ms | 497.995 ms | 1.01x | -1.09% |
| insert | 4.174 ms | 1.362 ms | 3.07x | -67.38% |
| lookup | 1.731 ms | 1.684 ms | 1.03x | -2.69% |
| update | 2.897 ms | 2.914 ms | 0.99x | +0.59% |
| delete | 37.303 ms | 4.438 ms | 8.41x | -88.10% |
| ascii_length | 18.352 us | 10.008 us | 1.83x | -45.47% |
| long_replace | 44.414 us | 44.266 us | 1.00x | -0.33% |

Zero-reuse was rejected because the small gains did not justify complexity.
The isolated private byte view reduced huge JSON parse from 306.220 to
282.897 ms (-7.62%), but repurposed the tuple conversion opcode for a string
view. Its owner-lifetime, 13-size, GC, tuple and JSON fixtures passed after
fixing a fixture guard. It is deferred until a dedicated compatible internal
mechanism can be designed; the production tuple and mutable byte-list
contracts remain unchanged. Cursor specialization, packed arrays, full
SIMD UTF-8 validation and managed byte builders remain future experiments.

### Final direct comparison — original HEAD vs integrated working tree

Baseline Git commit `35aa30ed4b35c6ac2c0121ad2008bba17e872306`, not the
pre-roadmap working tree or CP7. Fresh final executables use the integrated
CP8 production runtime. The five benchmark source files are byte-identical
between original and final; all ten executable hashes and both runtime
hashes were checked. macOS ARM64 (Apple M3 Pro), five randomized paired
rounds per case (seed 20261112, BENCH_SEED=17), profiling disabled, in-process
median per operation (map workloads per batch of 512 keys). All 21 cases
completed five successful runs on each side with identical checksums/output.
The direct comparison includes improvements already in the working tree at
CP0; speedups are measured directly and are not products of separate runs.

| Case | Before | After | Speedup | Time change |
| --- | ---: | ---: | ---: | ---: |
| json_parse | 514.621 us | 487.580 us | 1.06x | -5.25% |
| unicode_replace | 62.330 us | 46.984 us | 1.33x | -24.62% |
| arithmetic | 2.064 ns | 2.048 ns | 1.01x | -0.80% |
| branch_loop | 246.912 ns | 252.079 ns | 0.98x | +2.09% |
| fibonacci | 88.531 ns | 87.424 ns | 1.01x | -1.25% |
| list_128 | 165.417 ns | 162.701 ns | 1.02x | -1.64% |
| parse_nested | 20.183 ms | 18.936 ms | 1.07x | -6.18% |
| parse_long | 4.739 ms | 4.472 ms | 1.06x | -5.64% |
| parse_very_long | 66.353 ms | 62.145 ms | 1.07x | -6.34% |
| parse_huge | 318.721 ms | 302.994 ms | 1.05x | -4.93% |
| serialize_nested | 25.317 ms | 23.890 ms | 1.06x | -5.64% |
| serialize_long | 5.234 ms | 4.329 ms | 1.21x | -17.30% |
| serialize_very_long | 43.554 ms | 40.162 ms | 1.08x | -7.79% |
| escape | 47.429 ms | 22.850 ms | 2.08x | -51.82% |
| object | 574.806 ms | 496.403 ms | 1.16x | -13.64% |
| insert | 71.219 ms | 1.351 ms | 52.70x | -98.10% |
| lookup | 4.062 ms | 1.701 ms | 2.39x | -58.14% |
| update | 7.626 ms | 2.932 ms | 2.60x | -61.56% |
| delete | 128.897 ms | 4.483 ms | 28.75x | -96.52% |
| ascii_length | 204.504 us | 10.113 us | 20.22x | -95.05% |
| long_replace | 143.121 us | 44.641 us | 3.21x | -68.81% |

The four general controls range from -1.64% to +2.09% in elapsed time;
branch_loop is +2.09%. Small differences should be interpreted alongside the
raw samples and MAD. JSON parsing improves by about 5–6%, while ASCII length,
long replacement, escaping and string-key map operations show larger gains.
These are workload-specific native AOT measurements, not whole-program or
cross-language speedups.

**Final correctness gates:** 36/36 fixture/backend combinations (nine fixtures
on interpreter, JIT, AOT C++ and AOT V++); all four exact differential fixtures
(reference copy/GC, 37,711 UTF-8 lines, map, JSON thresholds); dynamic AOT
parity; standalone bulk primitive harnesses on ARM64 and Rosetta x86-64
(189,266 copy/prefix cases plus bounds checks each); Linux/Windows
cross-assembly; full macOS executable self-host with identical stage1/stage2
and no external build tools during the stages. The permanent reference-copy
fixture tests aliasing, overlapping shifts, deletion, nested references and
GC; map cardinality is checked using the key list because `độ dài(map)` is
not supported by the interpreter. The existing AOT V++ `xóa khóa` return
value inconsistency remains outside the scope of this performance change.
Linux/Windows native execution and performance have not been verified.

Runtime SHA-256: `cbe44d7a3f9305a03ff2d95b13557d20dbb704472805753c412138357aa8e68c`.
Self-host stage1/stage2 SHA-256: `289b7c1cc579a021105168f93de8935febfba873344145f72eec101aeb5b9db5`.

Local evidence under `build-benchmark-unicode-json-2026-11-12/`:
- `integration-cp8/original-head-comparison.json` — raw samples, MAD,
  exact checks, hashes, errors and medians for the direct comparison.
- `roadmap-cp8/comparison-variant-copy-strings-copy-refs-ascii-prefix-combined-zero-reuse-private-view.json` — variant selection.
- `integration-cp8/fixture-gates.log`, `dynamic-parity.log`, `cross-assembly.log`,
  `selfhost.log`, `verified.patch`, `verified-hashes.txt` — verification evidence.
- `baseline-tree/final-direct-ab/hashes.txt` and `integration-cp8/binary-hashes.txt` — binary provenance.

Re-run the direct timing using the preserved benchmark binaries and identical
inputs, with no competing builds/tests:

```bash
R=build-benchmark-unicode-json-2026-11-12
python3 "$R/roadmap-cp8/compare-original.py" "$R/integration-cp8"
```

The roadmap through CP8 is complete; further architecture experiments are
separate work and have not been integrated.


## CP9 — dedicated byte view and strict UTF-8, 2026-11-12

CP9 integrates dedicated opcodes 230 (immutable byte view retaining its owning
string) and 231 (strict UTF-8 validation). Opcode 144 remains tuple-from-list;
153 still returns an independent mutable byte list. Native AOT avoids copying
the parser input, while interpreter/JIT/C++ AOT compatibility materializes a
tuple. Production runtime remains V++ and ASM.

Direct comparison: 21 workloads, five randomized rounds per binary, identical
inputs/checks, no competing builds during the full timing run. Units below are
per operation; original means the preserved original HEAD binary.

| Workload | Original | CP8 | CP9 | Time vs CP8 |
|---|---:|---:|---:|---:|
| json_parse | 513.398 us | 483.494 us | 377.373 us | -21.95% |
| parse_nested | 20.520 ms | 19.198 ms | 13.308 ms | -30.68% |
| parse_long | 4.738 ms | 4.616 ms | 3.506 ms | -24.05% |
| parse_very_long | 65.907 ms | 62.514 ms | 48.166 ms | -22.95% |
| parse_huge | 319.988 ms | 309.059 ms | 222.792 ms | -27.91% |

Four general controls vary from -2.92% to +2.58% versus CP8. Fibonacci is +2.58%;
serialization/map/control differences should be read alongside samples/MAD.
These measurements do not establish a universal absence of regressions.

The matched language rerun verifies 215 samples across nine runners with
explicit coverage gaps. Unicode replace/count: V++ 45.479 us, Node 1.390 us,
Python 0.469 us, C++ 0.193 us. JSON parse/result access: V++ 366.969 us,
Node 0.439 us, Python 2.074 us, Ruby 2.287 us. There is no matching C/C++/Java/
Rust JSON case here. C/C++/Java/V++ i32 were rebuilt; Rust reuses the Nov 4
binary because rustc is unavailable. See `roadmap-cp9/language-manifest.json`.

**Why the gap remains:** the compiler can remove the local list and use raw
register arithmetic in list_128, while JSON still performs about 195 dynamic
binary operations, 25 runtime invocations and 52 allocations per benchmark
operation. These are approximate whole-process counter slopes, including
result access/harness, not isolated parse counters. AOT is active (`vm_step`
profile scope stays zero). Existing specialized-helper fallback counters do
not measure whether the entire parser is unboxed.

A separate controlled diagnostic keeps the same Unicode corpus, expected
codepoint counts and checksum: public replace/public length is 43.534 us;
intrinsic replace/public length is 19.291 us; both intrinsics are 12.801 us.
Five shuffled rounds of 4096 operations, profiler disabled. This identifies
large API/class/method overhead; it is not a new production optimization.
The runtime scan still pays many small memory/helper calls and stack loads/
stores. Priorities are general inlining/devirtualization, typed cursor/byte
lowering and safe direct/bulk loads, then block scanning and result allocation.

Correctness evidence: ABI agreement (216 opcodes, 248 intrinsics, 120 host
arities), four exact differential fixtures, ownership/GC, 36 positive fixture/
backend combinations, 18 native rejection combinations, four contract backends,
dynamic parity, Unix cross-assembly and exact macOS stage1/stage2 self-host.
Two permanent byte-view/GC fixtures were added. CP9 delta and source hashes
are preserved separately from the CP8 patch. Native Linux/Windows execution
has not been measured. The previous native 2,000,000-byte input cap remains;
C++ compatibility continues materializing bytes.

Local evidence: `build-benchmark-unicode-json-2026-11-12/roadmap-cp9/`:
`comparison-candidate.json`, `comparison-strict-only-view-only-candidate.json`,
`languages-cp9.json`, `language-manifest.json`, `gates.log`, `contract.log`,
`selfhost.log`, `cp9-delta.patch`, `integrated-source-hashes.json`,
`integrated-reuse.log`, and `diagnosis/`. The independent integrated runtime
rebuild was interrupted before producing an object. Integration verification
uses the exact tested candidate object after comparing all runtime module
sources byte-for-byte. Diagnosis details are in the local
`benchmark/UNICODE_JSON_DIAGNOSIS.md` (new Markdown files are ignored by the
repository's existing ignore rule).

Integrated main-source verification: 16/16 fixture/backend combinations PASS;
both benchmark executables rebuilt from main sources and all five JSON cases
match checks. Integrated medians differ from the candidate by -1.13% to +3.03%.
`integrated/comparison.json` records the four medians recovered from the
interrupted log (raw samples were not persisted for those four); the final
huge case includes full samples in `integrated/parse-huge-samples.json`.


## CP10a–CP12 — stateless wrappers and raw UTF-8 replacement

CP10a removes allocation of stateless utility instances from eleven string
wrappers and two private JSON classes. CP11 type-name caching is rejected:
although serialization improves by 20–24%, huge JSON parsing repeatedly
regresses (4.5–6.7%). CP10b generic forwarder metadata is also rejected.

CP12 is integrated: bounded word-at-a-time literal search for non-continuation
UTF-8 bytes, decoder traversal for continuation-start patterns, hoisted raw
addresses and span copying. No public API/ABI change. Compared with CP10a,
short Unicode replace/count improves 4.63%; dense, sparse and absent non-ASCII
long-string workloads improve 43.68%, 33.11% and 34.01%. Huge JSON repeat
changes +0.23%; the four general controls remain within approximately 2.3%.

All eleven V++ AOT fixtures, mixed 864-case C++ differential, malformed-byte
boundary corpus, large output and limit rejection parity pass. Independently
rebuilt integrated runtime and seven benchmark binaries match the candidate
byte-for-byte. macOS executable self-host stage1/stage2 and linker input
regressions pass. Local evidence is under `roadmap-cp11/` and `roadmap-cp12/`
inside `build-benchmark-unicode-json-2026-11-12/`. Native Linux/Windows timing
remains unverified.

## CP14 — direct canonical type-tag branches

CP14 is integrated into the working sources. The compiler fuses a direct
TYPE_OF, canonical type literal, equality/inequality and conditional branch
into a single native helper. Raw i32 cells and immutable byte views keep
their existing type semantics; bootstrap mode and generic wrapper calls
remain unchanged.

Seven randomized repeat rounds against CP12 (median, identical checksums):

| Workload | CP12 | CP14 | Latency change |
|---|---:|---:|---:|
| Unicode replacement | 23.452 µs | 15.267 µs | −34.90% |
| Long Unicode replacement | 22.820 µs | 15.215 µs | −33.33% |
| JSON parsing | 327.291 µs | 332.588 µs | +1.62% |
| Huge JSON parsing | 214.849 ms | 220.493 ms | +2.63% |
| Very-long JSON parsing | 47.648 ms | 46.356 ms | −2.71% |
| Very-long JSON serialization | 37.927 ms | 38.006 ms | +0.21% |

The modest JSON regressions are accepted in exchange for the substantial
Unicode improvements. Five initial rounds on 21 workloads and seven repeat
rounds on six selected workloads are retained in `roadmap-cp14/`. Arithmetic,
branching, Fibonacci and list controls remained approximately stable.
Eleven isolated V++ AOT fixtures, four execution configurations for type
branches, GC edges, cross-assembly symbol checks and exact macOS compiler
self-host passed. Three permanent type-branch fixtures are registered.

Independent runtime rebuild matches the candidate's machine text, normalized
relocations and symbol names, although Mach-O container metadata differs
between the internal and external linkers. The independently rebuilt
compiler and runtime generated six byte-identical benchmark executables.
Integrated regression rerun passed (23 fixture/backend combinations); see local
`build-benchmark-unicode-json-2026-11-12/roadmap-cp14/CHECKPOINT.md`.
Native Linux/Windows execution and a new synchronized cross-language
comparison remain outstanding.

### Final integrated three-round repeat

| Workload | CP12 | CP14 | Latency change |
| --- | ---: | ---: | ---: |
| unicode_replace | 23.8956 µs | 15.3750 µs | -35.66% |
| long_replace | 22.9482 µs | 15.3921 µs | -32.93% |
| parse_very_long | 46.5326 ms | 46.5823 ms | +0.11% |
| serialize_very_long | 39.6734 ms | 40.9102 ms | +3.12% |
| json_parse | 335.8896 µs | 332.2080 µs | -1.10% |
| parse_huge | 210.0795 ms | 225.5850 ms | +7.38% |

The custom executables print elapsed milliseconds, divided by iterations in the raw repeat script; workloads prints ns/op directly. Normalized medians are stored separately, leaving the raw evidence intact. Huge parsing regressed 7.38% in this repeat; the earlier seven-round repeat was +2.63%. Unicode improvement remains reproducible.


## CP15 — LENGTH/store fusion (rejected)

The isolated candidate passed targeted correctness and self-host checks, but length/store was 6.10% slower, ASCII length 7.11% slower, huge JSON 5.26% slower, and nested JSON only 3.15% faster. CP14 remains integrated. Native invalid LENGTH is an existing backend discrepancy (error=2, VM catches the error); differential probes preserved behavior. Five randomized rounds, checksums matched. See roadmap-cp15/CHECKPOINT.md and comparison-common.json / comparison-extra.json for details.

| Workload | CP14 (ns/op) | CP15 (ns/op) | Latency change |
|---|---:|---:|---:|
| length_store | 6802.272 | 7216.926 | +6.10% |
| json_parse | 333927.734 | 328724.610 | -1.56% |
| unicode_replace | 14676.758 | 14732.910 | +0.38% |
| arithmetic | 2.016 | 2.049 | +1.64% |
| branch_loop | 243.395 | 247.433 | +1.66% |
| fibonacci | 86.525 | 87.013 | +0.56% |
| list_128 | 161.488 | 162.201 | +0.44% |
| parse_nested | 12578999.995 | 12182499.990 | -3.15% |
| parse_long | 3585250.005 | 3584375.001 | -0.02% |
| parse_very_long | 47345499.990 | 48692000.000 | +2.84% |
| parse_huge | 212526000.000 | 223703000.000 | +5.26% |
| serialize_nested | 22113749.995 | 21681750.000 | -1.95% |
| serialize_long | 4379749.999 | 4321249.999 | -1.34% |
| serialize_very_long | 40736000.000 | 40333000.005 | -0.99% |
| escape | 22910250.000 | 22244125.000 | -2.91% |
| object | 489893375.000 | 484866125.000 | -1.03% |
| insert | 1356375.001 | 1356624.999 | +0.02% |
| lookup | 1657000.000 | 1654300.781 | -0.16% |
| update | 2872015.625 | 2858140.625 | -0.48% |
| delete | 4494499.996 | 4471249.998 | -0.52% |
| ascii_length | 5054.687 | 5414.062 | +7.11% |
| long_replace | 15164.062 | 14960.937 | -1.34% |


## CP16 — static TYPE_OF forwarding

CP16 recognizes a static one-argument function whose exact seven-instruction body only forwards TYPE_OF. The native branch calls a guarded type-tag comparison, preserving the frame-depth limit, missing-binding behavior, canonical type names, and branch-entry restrictions. No source-name heuristic or JSON-specific path is used. The C++ helper is compatibility-only.

Five randomized paired rounds (macOS ARM64), with equal checksums and median normalized latency, show a 76.91% reduction for type forwarding (4.33x speedup), 35.70% for long JSON serialization and 42.32% for very-long JSON serialization. Ordinary JSON parsing improves 2.02%; huge parsing is effectively unchanged. Unicode replacement (+7.00%) and map lookup (+6.11%) regress; ASCII length (+5.23%) is noisy. The user accepts modest regressions for material gains elsewhere. Arithmetic, branch, Fibonacci and list benchmarks are approximately unchanged.

Candidate validation: new fixture passed V++ AOT, C++ AOT, C++ VM JIT=0/1 (GC interval 1); baseline fixture matched; 14 existing AOT regression fixtures passed; depth-limit stdout/stderr/status matched CP14 (error=15, exit=1). Cross-assembly and symbol checks passed for five C++ wrapper targets and two V++ wrapper targets; native execution outside macOS ARM64 is unverified. Self-host stage1/stage2 exact SHA-256 289b7c1cc579a021105168f93de8935febfba873344145f72eec101aeb5b9db5; no external build tools. Native machine code contains 20 forwarding helper calls in workloads, 18 in json-large and extra each, 139 in forward-bench (including imported functions).

Initial fixture used a reserved word in a function name; corrected fixture and all four configurations passed. First explicit symbol check omitted VPP_NATIVE_DYNAMIC_RUNTIME=1; corrected invocation passed. Both issues were in test setup, not production source.

Integration: verified all five original hashes and non-target source files against the candidate before copying only the five target files. Added fixture/expected and registered it in run_tests.sh. Backups and integrated-source-hashes.json retained. git diff --check passed. Independent production rebuild PASS: 27 fixture/backend checks, seven byte-identical benchmark executables, runtime machine text/relocations/symbols identical, eight production source hashes verified. Native execution verified on macOS ARM64.

| Workload | CP14 (ns/op) | CP16 (ns/op) | Latency change |
|---|---:|---:|---:|
| type_forward | 18739.197 | 4327.181 | -76.91% |
| json_parse | 304181.641 | 298033.203 | -2.02% |
| unicode_replace | 13229.492 | 14155.762 | +7.00% |
| arithmetic | 1.953 | 1.951 | -0.11% |
| branch_loop | 237.253 | 237.286 | +0.01% |
| fibonacci | 84.383 | 84.245 | -0.16% |
| list_128 | 157.715 | 158.607 | +0.57% |
| parse_nested | 12020999.998 | 11849249.990 | -1.43% |
| parse_long | 3433250.002 | 3412124.999 | -0.62% |
| parse_very_long | 42383500.010 | 40882499.995 | -3.54% |
| parse_huge | 189883000.000 | 189789000.000 | -0.05% |
| serialize_nested | 19533249.990 | 18009999.990 | -7.80% |
| serialize_long | 3858999.997 | 2481500.008 | -35.70% |
| serialize_very_long | 35768000.005 | 20630499.990 | -42.32% |
| escape | 20168000.000 | 20165875.000 | -0.01% |
| object | 462755375.000 | 435151500.000 | -5.97% |
| insert | 1307750.001 | 1246749.997 | -4.66% |
| lookup | 1514542.969 | 1607015.625 | +6.11% |
| update | 2602601.562 | 2602671.875 | +0.00% |
| delete | 4011875.004 | 4016125.001 | +0.11% |
| ascii_length | 5082.031 | 5347.656 | +5.23% |
| long_replace | 13714.844 | 13687.500 | -0.20% |

See roadmap-cp16/CHECKPOINT.md and comparison-*.json for evidence.


## CP18 — guarded byte scan loops and byte-view escaping (integrated)

CP17 strict UTF-8 validation and immutable byte-view escaping are included in CP18. CP18 matches exact bytecode loop structure, slot identities, range/forbidden constants and branch targets; the V++ runtime checks types, distinct bindings, inline cursor and view bounds, then scans raw memory. Other cases execute the original bytecode. No C++ production optimization or function-name/benchmark heuristic. The emitted native runtime still spills temporaries and calls a machine byte-read primitive per byte; it is not a fully register-resident scan.

Five randomized three-way rounds (macOS ARM64), equal outputs and checksums. Controlled long single strings (5–10 KB) have especially large gains; these are not whole-document JSON throughput claims. Dense escaping and the separate escape workload regress around 8%; the standalone digit scan has no measurable gain. Integrated because general JSON parsing, nested serialization and ordinary long strings improve with negligible arithmetic/list changes. CP19 investigates the digit scan.

Candidate verification: 17 V++ AOT fixtures, four C++ AOT, six C++ VM checks (27 successful); two preexisting C++ direct emitter IR limitations matched baseline, negative/out-of-bounds byte view probes matched baseline stdout/stderr/status (error=4, exit=1). GC interval 1. Integration verified six original source hashes before copying six targeted files, four new fixture/expected paths and registering fixtures in run_tests.sh. Eleven integrated source hashes match; git diff --check passed. Backups, patch and hashes retained.

Independent production rebuild: runtime machine text (707172 bytes), 13683 relocations and 463 symbols match exactly. Four benchmark executables byte-identical to candidate. Compiler executable hashes differ between snapshot and production bootstrap; benchmark output binaries are identical. Native selfhost stage1/stage2 exact SHA-256 289b7c1cc579a021105168f93de8935febfba873344145f72eec101aeb5b9db5; no external build tools used after preparing the boundary. Cross-assembly script passed; explicit new-symbol checks passed on five C++ ABI targets (macOS ARM64/x86-64, Linux ARM64/x86-64, Windows x86-64) and two V++ ABI targets (macOS ARM64, Linux x86-64). Native execution verified only on macOS ARM64. Independent fixture suite passed all 35 backend checks (17 V++ AOT, 6 C++ AOT, 12 VM JIT on/off), exit 0.

| Workload | CP16 (ns/op) | CP17 (ns/op) | CP18 (ns/op) | CP16→CP18 |
|---|---:|---:|---:|---:|
| scan | 5672656.250 | 5708718.750 | 5681250.000 | +0.15% |
| ascii | 14243906.250 | 14352218.750 | 95406.249 | -99.33% |
| mixed | 42551312.500 | 42730187.500 | 640750.000 | -98.49% |
| dense | 21450750.000 | 21488750.000 | 23112125.000 | +7.75% |
| sparse | 78811625.000 | 81504875.000 | 69881875.000 | -11.33% |
| parse_ascii | 5390812.500 | 5413218.750 | 98906.251 | -98.17% |
| parse_mixed | 12386625.000 | 12815593.750 | 615406.249 | -95.03% |
| json_parse | 319208.984 | 320033.203 | 301023.438 | -5.70% |
| unicode_replace | 14175.781 | 13733.398 | 13766.113 | -2.89% |
| arithmetic | 1.952 | 1.952 | 1.955 | +0.14% |
| branch_loop | 237.452 | 237.829 | 238.567 | +0.47% |
| fibonacci | 84.893 | 85.461 | 84.408 | -0.57% |
| list_128 | 158.012 | 157.867 | 158.199 | +0.12% |
| parse_nested | 11437749.997 | 11437749.997 | 10268500.000 | -10.22% |
| parse_long | 3410125.002 | 3417999.998 | 3418875.001 | +0.26% |
| parse_very_long | 42173000.010 | 41888500.005 | 41778500.020 | -0.94% |
| parse_huge | 195088000.000 | 203048000.000 | 194369000.000 | -0.37% |
| serialize_nested | 18315249.995 | 17383000.003 | 13909750.000 | -24.05% |
| serialize_long | 2539250.001 | 2528624.996 | 2535499.998 | -0.15% |
| serialize_very_long | 21892000.020 | 21084000.020 | 21065999.985 | -3.77% |
| escape | 21560750.000 | 18729250.000 | 23149500.000 | +7.37% |
| object | 444685375.000 | 436804875.000 | 418153125.000 | -5.97% |

Evidence: roadmap-cp18/CHECKPOINT.md, comparison-*.json, integrated-verification.log, selfhost.log, cross-assembly.log and cross-symbols/.

## CP19 — local heap cursor in native byte scan

Integrated after 18 V++ AOT fixture passes, 1 C++ AOT pass (14 heap/closure cases), 2 error-parity probes, and unchanged VM direct-emitter limitation in 2 JIT modes. All tests at GC interval 1. V2 splits eager scope guard to avoid vm_map_get_int(0, slot). Local scope identity is checked; global fallback remains. The loop updates the same shared boxed cell once, using aot_ghi_i32; no new ABI or C++ production dependency.

Five paired randomized samples per case, all output checks equal. ns/op medians:

| Case | CP18 | CP19 | Change |
|---|---:|---:|---:|
| scan | 5,820,750.00 | 61,343.75 | -98.95% |
| ascii | 100,875.00 | 100,937.50 | +0.06% |
| mixed | 653,750.00 | 654,125.00 | +0.06% |
| dense | 23,657,875.00 | 23,620,000.00 | -0.16% |
| sparse | 70,002,875.00 | 69,857,125.00 | -0.21% |
| parse_ascii | 98,625.00 | 99,062.50 | +0.44% |
| parse_mixed | 616,562.50 | 616,937.50 | +0.06% |
| json_parse | 301,472.66 | 301,431.64 | -0.01% |
| unicode_replace | 14,449.22 | 14,469.73 | +0.14% |
| arithmetic | 1.95 | 1.95 | +0.00% |
| branch_loop | 237.68 | 237.54 | -0.06% |
| fibonacci | 84.40 | 84.49 | +0.11% |
| list_128 | 157.94 | 157.83 | -0.07% |
| parse_nested | 10,258,250.00 | 10,267,500.00 | +0.09% |
| parse_long | 3,420,625.00 | 3,426,125.00 | +0.16% |
| parse_very_long | 43,837,499.98 | 43,891,000.01 | +0.12% |
| parse_huge | 202,166,000.00 | 202,162,000.00 | -0.00% |
| serialize_nested | 13,858,249.99 | 13,895,750.00 | +0.27% |
| serialize_long | 2,625,500.00 | 2,632,500.00 | +0.27% |
| serialize_very_long | 21,792,000.00 | 21,868,999.99 | +0.35% |
| escape | 22,683,375.00 | 23,176,125.00 | +2.17% |
| object | 435,323,250.00 | 435,854,500.00 | +0.12% |

Scan probe: 5120 digit bytes, approximately 94.9x faster; this is a controlled loop, not general JSON parsing. Standard JSON and Unicode workloads are effectively unchanged. Escape +2.17%; accepted as a modest measured regression.

Original CP18 source hashes all matched. Backups and delta saved. Independent production rebuild PASS: 707684 machine-text bytes, 13686 relocations and 463 symbols identical; four benchmark binaries byte-identical; five root fixture passes at GC interval 1. All 13 integrated source hashes verified.

### CP19 cross-language rebenchmark (macOS ARM64)

Five randomized rounds, 200 validated samples, 3 warmup batches, target 50 ms. Units below: ns/op (median). Reused the 7 verified comparator runners from Nov 11; all 18 benchmark source hashes and the Java class matched. Rust omitted because no current verified runner/toolchain was available. C, C++, and Java do not have a JSON workload in this manifest. Comparison with the Nov 11 report is between separate runs, not a controlled A/B.

| Case | V++ AOT | V++ i32 | C | C++ | Node | Java | Python | Ruby |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| arithmetic | 1.950 | 1.139 | 0.799 | 0.809 | 1.103 | 1.343 | 94.213 | 81.482 |
| branch_loop | 238.130 | 137.515 | 196.391 | 181.686 | 150.097 | 67.722 | 4,261.326 | 6,576.374 |
| fibonacci | 85.936 | 19.871 | 8.892 | 8.790 | 11.209 | 4.455 | 469.291 | 1,038.097 |
| list_128 | 157.696 | — | — | 238.637 | 521.392 | 358.790 | 5,072.419 | 7,417.328 |
| unicode_replace | 14,800.676 | — | — | 179.266 | 1,314.284 | 180.851 | 450.017 | 1,200.919 |
| json_parse | 300,048.611 | — | — | — | 419.892 | — | 1,976.175 | 2,115.944 |

Relative to Nov 11 V++ AOT: Unicode replace 4.06x faster; JSON parse 1.67x faster. JSON and Unicode are still substantially slower than the comparator runtimes on these workloads.

Raw report: `build-benchmark-unicode-json-2026-11-12/roadmap-cp19/languages-cp19.json` (SHA256 `18dabbbc40f4290f0c803e552a744d6965d0322766f3bc5c8312249e6a3cbcf3`).


### CP20 controlled A/B

25 cases, five paired rounds, all checks match.

| Case | Baseline ns/op | Candidate ns/op | Change |
|---|---:|---:|---:|
| json_parse | 296,537.109 | 298,224.609 | +0.57% |
| unicode_replace | 14,460.938 | 14,364.258 | -0.67% |
| arithmetic | 1.950 | 1.951 | +0.03% |
| branch_loop | 238.764 | 237.450 | -0.55% |
| fibonacci | 84.421 | 84.757 | +0.40% |
| list_128 | 157.604 | 157.841 | +0.15% |
| parse_nested | 10,049,500.002 | 9,839,749.992 | -2.09% |
| parse_long | 3,406,750.001 | 3,429,000.005 | +0.65% |
| parse_very_long | 42,759,999.990 | 43,008,500.010 | +0.58% |
| parse_huge | 199,305,000.000 | 199,417,000.100 | +0.06% |
| serialize_nested | 13,624,500.008 | 13,670,000.002 | +0.33% |
| serialize_long | 2,606,375.001 | 2,597,500.004 | -0.34% |
| serialize_very_long | 21,559,999.975 | 21,567,999.990 | +0.04% |
| escape | 22,590,500.000 | 22,435,625.000 | -0.69% |
| object | 428,741,625.000 | 430,996,250.000 | +0.53% |
| normal | 1,486,750.001 | 1,487,906.251 | +0.08% |
| overflow | 45,284,750.000 | 44,419,750.000 | -1.91% |
| long | 69,996,250.000 | 63,891,000.000 | -8.72% |
| scan | 61,312.500 | 61,312.500 | +0.00% |
| ascii | 100,031.249 | 99,562.500 | -0.47% |
| mixed | 649,218.751 | 645,874.999 | -0.52% |
| dense | 23,173,000.000 | 23,126,125.000 | -0.20% |
| sparse | 67,181,750.000 | 69,790,625.000 | +3.88% |
| parse_ascii | 100,781.251 | 100,968.750 | +0.19% |
| parse_mixed | 631,093.750 | 629,437.499 | -0.26% |

CP20 integrated; seven independent root fixtures PASS, four benchmark binaries identical. Standard workloads rebuilt from root source differs in embedded paths; additional output parity pending. Sparse escape +3.88% accepted.


### CP21 controlled A/B

28 cases, five paired rounds, all checks match.

| Case | Baseline ns/op | Candidate ns/op | Change |
|---|---:|---:|---:|
| json_parse | 298,341.797 | 298,789.062 | +0.15% |
| unicode_replace | 14,620.117 | 14,492.676 | -0.87% |
| arithmetic | 1.951 | 1.952 | +0.09% |
| branch_loop | 237.650 | 237.984 | +0.14% |
| fibonacci | 84.459 | 84.445 | -0.02% |
| list_128 | 157.837 | 157.738 | -0.06% |
| parse_nested | 10,220,750.005 | 10,114,749.998 | -1.04% |
| parse_long | 3,412,749.999 | 3,430,874.996 | +0.53% |
| parse_very_long | 43,127,500.000 | 43,797,500.015 | +1.55% |
| parse_huge | 206,990,000.000 | 204,531,000.000 | -1.19% |
| serialize_nested | 13,861,249.998 | 13,882,500.007 | +0.15% |
| serialize_long | 2,622,625.001 | 2,632,249.996 | +0.37% |
| serialize_very_long | 21,810,000.000 | 21,747,999.995 | -0.28% |
| escape | 22,757,625.000 | 22,705,375.000 | -0.23% |
| object | 431,731,875.000 | 433,867,000.000 | +0.49% |
| normal | 1,552,968.750 | 1,551,062.500 | -0.12% |
| overflow | 44,878,250.000 | 44,887,250.000 | +0.02% |
| long | 64,418,500.000 | 64,556,250.000 | +0.21% |
| scan | 61,218.752 | 61,312.500 | +0.15% |
| ascii | 99,593.751 | 99,562.502 | -0.03% |
| mixed | 653,531.252 | 640,250.001 | -2.03% |
| dense | 23,130,375.000 | 23,091,875.000 | -0.17% |
| sparse | 69,827,500.000 | 69,857,750.000 | +0.04% |
| parse_ascii | 95,031.250 | 98,406.250 | +3.55% |
| parse_mixed | 616,250.001 | 615,374.999 | -0.14% |
| set_scan | 2,358,406.251 | 14,156.250 | -99.40% |
| space_parse | 3,620,937.500 | 66,875.000 | -98.15% |
| pretty | 7,610,250.000 | 5,357,312.497 | -29.60% |

CP21 integrated; candidate correctness PASS (10 fixtures at GC interval 1). Four-member byte scan 166.6x, whitespace-heavy JSON 54.1x, pretty numeric array 1.42x. ASCII parse +3.55% accepted. Independent production verification pending.

Independent root verification PASS: runtime machine text/relocations/symbols exact, six benchmark executables byte-identical, ten GC interval 1 fixtures PASS. C++ AOT and VM JIT=0/1 PASS. Native selfhosting PASS with prior verified stage2 seed: both stages SHA-256 289b7c1cc579a021105168f93de8935febfba873344145f72eec101aeb5b9db5; layout, Unicode/runtime probes, malformed linker input regressions PASS; no external build tools. Earlier bootstrap seed failure was resolved by using native seed. All 17 accumulated source hashes and git diff --check PASS.

## CP22 — native object-key scan (CP21 vs CP22)

31 cases, five randomized paired rounds, checksum/output exact. Eight candidate GC=1 fixtures and baseline new key fixture PASS. Native scan lowering verified in machine code. Integrated JSON and test registration only. Long key time unchanged; standard JSON -6.73%, nested -5.02%. Independent root verification pending.

| Case | CP21 ns/op | CP22 ns/op | Change |
|---|---:|---:|---:|
| key_long | 869,000.003 | 869,374.998 | +0.04% |
| key_mixed | 45,191,250.000 | 44,767,500.000 | -0.94% |
| key_short | 525,187.500 | 517,343.748 | -1.49% |
| json_parse | 298,234.375 | 278,167.969 | -6.73% |
| unicode_replace | 14,473.633 | 14,451.660 | -0.15% |
| arithmetic | 1.952 | 1.952 | -0.02% |
| branch_loop | 238.827 | 237.988 | -0.35% |
| fibonacci | 85.454 | 84.308 | -1.34% |
| list_128 | 157.921 | 157.707 | -0.14% |
| scan | 61,250.001 | 61,249.999 | -0.00% |
| ascii | 99,906.251 | 99,593.751 | -0.31% |
| mixed | 639,812.501 | 648,624.999 | +1.38% |
| dense | 23,108,875.000 | 23,122,125.000 | +0.06% |
| sparse | 69,740,125.000 | 69,744,875.000 | +0.01% |
| parse_ascii | 97,749.999 | 98,500.000 | +0.77% |
| parse_mixed | 617,500.002 | 617,124.999 | -0.06% |
| normal | 1,548,625.000 | 1,552,781.250 | +0.27% |
| overflow | 44,922,250.000 | 44,957,250.025 | +0.08% |
| long | 64,384,000.000 | 64,571,250.000 | +0.29% |
| set_scan | 14,093.751 | 14,156.250 | +0.44% |
| space_parse | 70,062.500 | 69,812.503 | -0.36% |
| pretty | 5,355,999.999 | 5,367,500.000 | +0.21% |
| parse_nested | 10,272,750.005 | 9,756,750.003 | -5.02% |
| parse_long | 3,435,250.006 | 3,434,125.006 | -0.03% |
| parse_very_long | 43,889,999.985 | 43,821,999.995 | -0.15% |
| parse_huge | 203,357,000.000 | 203,547,000.000 | +0.09% |
| serialize_nested | 13,862,250.000 | 13,974,500.000 | +0.81% |
| serialize_long | 2,617,375.001 | 2,626,249.999 | +0.34% |
| serialize_very_long | 21,858,000.010 | 21,828,999.995 | -0.13% |
| escape | 22,802,000.000 | 22,824,625.000 | +0.10% |
| object | 437,439,000.000 | 433,111,875.000 | -0.99% |

Independent root verification PASS: all seven benchmark binaries byte-identical and eight GC interval 1 fixtures passed. All 19 accumulated source hashes and git diff --check PASS before verification.

## CP23 — escaped string native span scan (CP22 vs CP23)

35 cases, five randomized paired rounds, checksum/output exact. Nine candidate GC=1 fixtures PASS; nine valid and eight malformed cases exact baseline output and error positions. Escaped long object key 24.5x; dense escape parse -0.38%, sparse escape parse +1.89%, standard JSON +0.18%. Integrated JSON and fixture registration only. Independent root verification pending.

| Case | CP22 ns/op | CP23 ns/op | Change |
|---|---:|---:|---:|
| escape-parse/parse_dense | 25,755,000.000 | 25,656,750.000 | -0.38% |
| escape-parse/parse_sparse | 28,781,000.000 | 29,324,000.000 | +1.89% |
| escape-parse/parse_ascii | 100,281.250 | 100,156.249 | -0.12% |
| escape-parse/parse_mixed | 616,437.500 | 618,968.751 | +0.41% |
| keys/key_long | 888,999.999 | 890,000.001 | +0.11% |
| keys/key_mixed | 45,136,000.000 | 1,840,499.997 | -95.92% |
| keys/key_short | 528,312.501 | 526,343.750 | -0.37% |
| common/json_parse | 274,175.781 | 274,681.641 | +0.18% |
| common/unicode_replace | 14,461.426 | 14,437.500 | -0.17% |
| common/arithmetic | 1.950 | 1.951 | +0.03% |
| common/branch_loop | 237.623 | 238.094 | +0.20% |
| common/fibonacci | 84.389 | 84.399 | +0.01% |
| common/list_128 | 158.188 | 157.700 | -0.31% |
| scan/scan | 61,281.249 | 61,249.999 | -0.05% |
| scan/ascii | 99,593.749 | 99,625.001 | +0.03% |
| scan/mixed | 643,750.001 | 651,406.249 | +1.19% |
| scan/dense | 23,211,250.000 | 23,025,500.000 | -0.80% |
| scan/sparse | 69,889,250.000 | 69,624,250.000 | -0.38% |
| scan/parse_ascii | 100,562.502 | 100,562.500 | -0.00% |
| scan/parse_mixed | 613,625.001 | 615,343.751 | +0.28% |
| numbers/normal | 1,553,625.001 | 1,552,375.000 | -0.08% |
| numbers/overflow | 45,004,250.000 | 45,065,500.000 | +0.14% |
| numbers/long | 64,475,750.000 | 64,471,250.000 | -0.01% |
| set/set_scan | 14,156.250 | 14,125.001 | -0.22% |
| set/space_parse | 69,937.497 | 69,750.000 | -0.27% |
| set/pretty | 5,368,750.002 | 5,364,999.998 | -0.07% |
| extra/parse_nested | 9,627,000.005 | 9,795,249.997 | +1.75% |
| extra/parse_long | 3,428,874.999 | 3,436,124.995 | +0.21% |
| extra/parse_very_long | 43,754,500.000 | 43,856,999.995 | +0.23% |
| extra/parse_huge | 203,508,000.000 | 204,084,000.000 | +0.28% |
| extra/serialize_nested | 13,960,749.998 | 13,961,249.993 | +0.00% |
| extra/serialize_long | 2,628,500.000 | 2,612,875.000 | -0.59% |
| extra/serialize_very_long | 21,868,499.995 | 21,887,499.990 | +0.09% |
| extra/escape | 23,215,625.000 | 23,140,375.000 | -0.32% |
| extra/object | 435,760,625.000 | 435,911,375.000 | +0.03% |

CP23 independent root verification: eight rebuilt executables byte-identical, nine GC=1 fixtures PASS; all 21 accumulated source hashes match; git diff --check PASS.

## CP24 — direct native string intrinsics (CP23 vs CP24)

35 cases, five randomized paired rounds, outputs/checksums exact. Eleven GC=1 candidate fixtures PASS; five wrong-type/negative-index probes matched baseline exit status, stdout and stderr. Six intrinsics bypass argument scratch and generic host dispatch with rooted stack inputs. Unicode -2.35%, nested serialization -2.01%, dense escape parsing -1.95%; common JSON +1.12% accepted; arithmetic +0.04%. Integrated runtime and test registration only. Independent root verification pending.

| Case | CP23 ns/op | CP24 ns/op | Change |
|---|---:|---:|---:|
| common/json_parse | 274,414.062 | 277,484.375 | +1.12% |
| common/unicode_replace | 14,442.871 | 14,104.004 | -2.35% |
| common/arithmetic | 1.951 | 1.952 | +0.04% |
| common/branch_loop | 237.556 | 238.062 | +0.21% |
| common/fibonacci | 84.445 | 84.607 | +0.19% |
| common/list_128 | 157.688 | 158.306 | +0.39% |
| escape-parse/parse_dense | 24,952,500.000 | 24,465,875.000 | -1.95% |
| escape-parse/parse_sparse | 28,886,000.000 | 28,926,250.000 | +0.14% |
| escape-parse/parse_ascii | 100,937.501 | 99,156.249 | -1.76% |
| escape-parse/parse_mixed | 619,031.251 | 616,531.251 | -0.40% |
| extra/parse_nested | 9,626,250.000 | 9,535,999.997 | -0.94% |
| extra/parse_long | 3,426,875.002 | 3,424,625.001 | -0.07% |
| extra/parse_very_long | 43,767,499.985 | 43,796,999.990 | +0.07% |
| extra/parse_huge | 203,543,000.000 | 203,368,000.000 | -0.09% |
| extra/serialize_nested | 13,958,999.993 | 13,678,000.002 | -2.01% |
| extra/serialize_long | 2,628,249.995 | 2,630,250.000 | +0.08% |
| extra/serialize_very_long | 21,878,500.015 | 21,877,000.005 | -0.01% |
| extra/escape | 23,114,000.000 | 22,913,375.000 | -0.87% |
| extra/object | 435,576,250.000 | 432,433,750.000 | -0.72% |
| keys/key_long | 872,375.004 | 864,500.001 | -0.90% |
| keys/key_mixed | 1,844,500.005 | 1,820,749.998 | -1.29% |
| keys/key_short | 526,125.001 | 528,656.250 | +0.48% |
| numbers/normal | 1,550,937.500 | 1,552,781.250 | +0.12% |
| numbers/overflow | 44,940,500.000 | 45,215,750.000 | +0.61% |
| numbers/long | 64,489,500.000 | 64,423,750.000 | -0.10% |
| scan/scan | 61,281.249 | 61,312.500 | +0.05% |
| scan/ascii | 99,218.750 | 98,968.750 | -0.25% |
| scan/mixed | 647,875.000 | 637,093.751 | -1.66% |
| scan/dense | 22,929,125.000 | 22,932,000.000 | +0.01% |
| scan/sparse | 69,681,375.000 | 69,233,375.000 | -0.64% |
| scan/parse_ascii | 98,656.250 | 97,562.499 | -1.11% |
| scan/parse_mixed | 615,093.749 | 613,218.751 | -0.30% |
| set/set_scan | 14,093.751 | 14,125.001 | +0.22% |
| set/space_parse | 66,750.001 | 65,562.498 | -1.78% |
| set/pretty | 5,315,499.999 | 5,362,000.000 | +0.87% |

Independent root verification: runtime machine text 711356 bytes, 13713 relocations and 463 symbols exact; eight benchmark binaries byte-identical; all eleven GC=1 fixtures PASS; all 23 accumulated source hashes exact. CP24 complete.


## CP27 — word UTF-8 scan and guarded native decimal loop (CP24 vs CP27)

41 cases, five randomized paired rounds; checksums and stderr exact. CP25/26 independent verification accepted, combined candidate passes 13 GC=1 fixtures. UTF-8 mixed scan uses word loads with scalar fallback; decimal loop preserves dynamic fallback, local cell identity and bounds. Production runtime is V++/ASM; C++ compatibility falls back to original bytecode. Root verification pending.

| Case | CP24 ns/op | CP27 ns/op | Change |
|---|---:|---:|---:|
| common/json_parse | 277,437.500 | 272,564.453 | -1.76% |
| common/unicode_replace | 14,110.840 | 13,663.574 | -3.17% |
| common/arithmetic | 1.989 | 1.953 | -1.81% |
| common/branch_loop | 247.389 | 242.544 | -1.96% |
| common/fibonacci | 86.049 | 86.035 | -0.02% |
| common/list_128 | 160.919 | 160.904 | -0.01% |
| utf8/strict_mixed | 105,292.969 | 68,609.375 | -34.84% |
| utf8/strict_ascii | 3,105.469 | 3,097.656 | -0.25% |
| utf8/length_mixed | 115,927.734 | 78,406.250 | -32.37% |
| utf8/length_ascii | 14,763.672 | 14,791.016 | +0.19% |
| decimal/decimal | 84,194.580 | 27,902.832 | -66.86% |
| decimal/json_decimal | 4,405,062.500 | 4,391,093.750 | -0.32% |
| scan/scan | 62,906.250 | 62,843.749 | -0.10% |
| scan/ascii | 101,687.500 | 101,437.500 | -0.25% |
| scan/mixed | 663,468.750 | 488,906.249 | -26.31% |
| scan/dense | 23,174,500.000 | 23,363,875.000 | +0.82% |
| scan/sparse | 70,675,375.000 | 70,605,000.000 | -0.10% |
| scan/parse_ascii | 99,468.749 | 99,343.751 | -0.13% |
| scan/parse_mixed | 626,875.000 | 456,249.999 | -27.22% |
| extra/parse_nested | 9,750,749.990 | 9,795,750.008 | +0.46% |
| extra/parse_long | 3,507,249.996 | 3,507,500.000 | +0.01% |
| extra/parse_very_long | 44,742,500.005 | 44,828,999.995 | +0.19% |
| extra/parse_huge | 207,733,000.000 | 207,582,000.000 | -0.07% |
| extra/serialize_nested | 13,957,000.002 | 13,957,250.000 | +0.00% |
| extra/serialize_long | 2,681,500.002 | 2,682,749.994 | +0.05% |
| extra/serialize_very_long | 22,322,499.990 | 22,266,499.995 | -0.25% |
| extra/escape | 23,378,750.000 | 23,460,625.000 | +0.35% |
| extra/object | 449,332,500.000 | 444,583,375.000 | -1.06% |
| numbers/normal | 1,586,312.499 | 1,584,468.750 | -0.12% |
| numbers/overflow | 45,864,750.000 | 45,824,000.000 | -0.09% |
| numbers/long | 65,670,500.000 | 65,865,500.000 | +0.30% |
| keys/key_long | 902,750.000 | 908,000.000 | +0.58% |
| keys/key_mixed | 1,932,750.002 | 1,927,000.001 | -0.30% |
| keys/key_short | 531,468.751 | 531,062.501 | -0.08% |
| set/set_scan | 14,750.000 | 14,718.750 | -0.21% |
| set/space_parse | 73,750.000 | 72,124.999 | -2.20% |
| set/pretty | 5,802,875.001 | 5,828,937.501 | +0.45% |
| escape-parse/parse_dense | 25,997,500.000 | 25,987,249.988 | -0.04% |
| escape-parse/parse_sparse | 29,176,000.000 | 29,586,750.000 | +1.41% |
| escape-parse/parse_ascii | 101,031.249 | 101,187.501 | +0.15% |
| escape-parse/parse_mixed | 626,937.499 | 458,562.499 | -26.86% |

CP27 root verification: PASS. Independently rebuilt runtime machine text (723456 bytes), 13726 relocations and 466 symbols equal; 10 benchmark executables byte-identical; 13 GC=1 fixtures pass. Full native executable selfhost PASS: stage1 and stage2 SHA256 289b7c1cc579a021105168f93de8935febfba873344145f72eec101aeb5b9db5; Unicode/runtime probes and malformed Mach-O regressions PASS; no external build tools.


## CP28 — generic constant-offset index fusion (CP27 vs CP28)

41 cases, five randomized paired rounds; checksums and stderr exact. Candidate passes 14 GC=1 fixtures. Constant-offset index fusion preserves dynamic fallback, errors, and local cell identity; C++ compatibility uses the original stack operations. Root verification pending.

| Case | CP27 ns/op | CP28 ns/op | Change |
|---|---:|---:|---:|
| common/json_parse | 301,660.156 | 250,919.922 | -16.82% |
| common/unicode_replace | 19,404.297 | 19,087.891 | -1.63% |
| common/arithmetic | 1.950 | 1.953 | +0.16% |
| common/branch_loop | 238.178 | 237.528 | -0.27% |
| common/fibonacci | 84.291 | 84.491 | +0.24% |
| common/list_128 | 157.574 | 157.982 | +0.26% |
| utf8/strict_mixed | 67,044.922 | 67,128.906 | +0.13% |
| utf8/strict_ascii | 3,761.719 | 3,585.937 | -4.67% |
| utf8/length_mixed | 134,435.547 | 127,222.656 | -5.37% |
| utf8/length_ascii | 22,326.172 | 22,123.047 | -0.91% |
| decimal/decimal | 35,164.062 | 33,120.605 | -5.81% |
| decimal/json_decimal | 5,227,781.250 | 5,387,343.750 | +3.05% |
| scan/scan | 58,593.750 | 60,343.750 | +2.99% |
| scan/ascii | 96,937.500 | 90,343.749 | -6.80% |
| scan/mixed | 493,812.502 | 492,343.750 | -0.30% |
| scan/dense | 22,756,000.000 | 22,740,000.000 | -0.07% |
| scan/sparse | 66,648,375.000 | 68,462,125.000 | +2.72% |
| scan/parse_ascii | 97,593.749 | 97,531.250 | -0.06% |
| scan/parse_mixed | 430,687.500 | 447,562.501 | +3.92% |
| extra/parse_nested | 13,412,249.998 | 9,744,999.990 | -27.34% |
| extra/parse_long | 3,442,999.996 | 3,153,625.004 | -8.40% |
| extra/parse_very_long | 42,385,000.020 | 38,733,500.005 | -8.62% |
| extra/parse_huge | 203,411,000.000 | 173,862,000.000 | -14.53% |
| extra/serialize_nested | 13,411,249.995 | 13,177,500.010 | -1.74% |
| extra/serialize_long | 2,564,874.999 | 2,610,750.005 | +1.79% |
| extra/serialize_very_long | 21,381,500.005 | 21,433,000.000 | +0.24% |
| extra/escape | 22,862,375.000 | 22,065,500.000 | -3.49% |
| extra/object | 426,166,750.000 | 368,669,250.000 | -13.49% |
| numbers/normal | 1,550,906.250 | 1,455,531.251 | -6.15% |
| numbers/overflow | 44,126,250.000 | 43,846,750.000 | -0.63% |
| numbers/long | 62,860,750.000 | 62,665,250.000 | -0.31% |
| keys/key_long | 863,749.996 | 818,499.997 | -5.24% |
| keys/key_mixed | 1,816,250.011 | 1,774,000.004 | -2.33% |
| keys/key_short | 512,656.251 | 473,218.750 | -7.69% |
| set/set_scan | 18,781.250 | 19,468.749 | +3.66% |
| set/space_parse | 68,812.501 | 66,062.503 | -4.00% |
| set/pretty | 5,320,312.500 | 5,262,562.499 | -1.09% |
| escape-parse/parse_dense | 24,432,375.000 | 24,747,250.000 | +1.29% |
| escape-parse/parse_sparse | 28,209,500.000 | 28,226,750.000 | +0.06% |
| escape-parse/parse_ascii | 99,093.750 | 99,062.501 | -0.03% |
| escape-parse/parse_mixed | 449,468.751 | 449,406.251 | -0.01% |

CP28 root verification: PASS. Independently rebuilt runtime machine text (726604 bytes), 13751 relocations and 467 symbols equal; 10 benchmark executables byte-identical; 14 GC=1 fixtures pass. Full native executable selfhost PASS using CP27 native seed: stage1 and stage2 SHA256 289b7c1cc579a021105168f93de8935febfba873344145f72eec101aeb5b9db5; Unicode/runtime probes and malformed Mach-O regressions PASS; no external build tools. Initial attempt with bootstrap CLI selected the external assembler and correctly failed the tool guard (99); retry used the verified native executable seed.


## CP30 — generic native i32 run scanner (CP28 vs CP30)

41 cases, five randomized paired rounds; checksums and stderr exact. Candidate passes 15 GC=1 fixtures, 184 differential cases (182 exact, two intentional malformed-input fixes), 10 invalid-argument cases, and compatibility on C++ AOT and VM. Native i32 compiler self-host converges; dynamic opcode execution verified separately. Root verification PASS: independently rebuilt runtime text (734904 bytes), 13838 relocations, 468 symbols; all V++ modules and 10 same-source executable pairs exact; 15 GC=1 fixtures, 184 differential outcomes, 10 error probes, C++ AOT and VM JIT off/on pass. Absolute paths affect binary layout, so comparisons use the same source path.

| Case | CP28 ns/op | CP30 ns/op | Change |
|---|---:|---:|---:|
| common/json_parse | 267,033.203 | 154,964.844 | -41.97% |
| common/unicode_replace | 14,173.828 | 14,147.461 | -0.19% |
| common/arithmetic | 2.044 | 2.042 | -0.12% |
| common/branch_loop | 244.326 | 243.792 | -0.22% |
| common/fibonacci | 86.823 | 86.740 | -0.10% |
| common/list_128 | 161.041 | 161.797 | +0.47% |
| utf8/strict_mixed | 68,421.875 | 68,349.609 | -0.11% |
| utf8/strict_ascii | 3,115.234 | 3,060.547 | -1.76% |
| utf8/length_mixed | 79,966.797 | 79,662.109 | -0.38% |
| utf8/length_ascii | 14,947.266 | 14,582.031 | -2.44% |
| decimal/decimal | 28,402.344 | 28,137.695 | -0.93% |
| decimal/json_decimal | 4,429,218.750 | 105,093.751 | -97.63% |
| scan/scan | 63,375.000 | 63,156.249 | -0.35% |
| scan/ascii | 104,593.750 | 107,875.001 | +3.14% |
| scan/mixed | 495,312.501 | 496,593.749 | +0.26% |
| scan/dense | 23,859,250.000 | 23,929,625.000 | +0.29% |
| scan/sparse | 72,650,750.000 | 73,009,625.000 | +0.49% |
| scan/parse_ascii | 102,031.250 | 102,812.499 | +0.77% |
| scan/parse_mixed | 470,031.250 | 471,375.000 | +0.29% |
| extra/parse_nested | 9,246,749.998 | 7,650,000.005 | -17.27% |
| extra/parse_long | 3,252,875.000 | 116,875.000 | -96.41% |
| extra/parse_very_long | 42,258,000.015 | 916,999.995 | -97.83% |
| extra/parse_huge | 184,554,000.000 | 7,955,999.970 | -95.69% |
| extra/serialize_nested | 14,037,749.990 | 13,942,000.002 | -0.68% |
| extra/serialize_long | 2,682,999.999 | 2,669,999.994 | -0.48% |
| extra/serialize_very_long | 22,496,000.020 | 22,756,000.010 | +1.16% |
| extra/escape | 24,087,500.000 | 24,144,875.000 | +0.24% |
| extra/object | 392,580,125.000 | 392,967,625.000 | +0.10% |
| numbers/normal | 1,508,500.000 | 92,250.001 | -93.88% |
| numbers/overflow | 47,392,750.000 | 47,721,000.000 | +0.69% |
| numbers/long | 66,118,000.000 | 66,034,500.000 | -0.13% |
| keys/key_long | 843,374.997 | 838,624.999 | -0.56% |
| keys/key_mixed | 1,809,500.009 | 1,821,999.997 | +0.69% |
| keys/key_short | 505,500.000 | 506,843.749 | +0.27% |
| set/set_scan | 14,500.000 | 14,468.750 | -0.22% |
| set/space_parse | 67,437.500 | 53,312.499 | -20.95% |
| set/pretty | 5,454,000.000 | 119,937.498 | -97.80% |
| escape-parse/parse_dense | 25,012,250.000 | 24,990,250.012 | -0.09% |
| escape-parse/parse_sparse | 28,924,750.000 | 29,299,750.000 | +1.30% |
| escape-parse/parse_ascii | 105,937.500 | 110,062.499 | +3.89% |
| escape-parse/parse_mixed | 465,250.000 | 465,781.249 | +0.11% |


## CP31 — bounded decimal scanning (CP30 vs CP31)

41 regular cases and one targeted prefix case, five randomized paired rounds. 16 GC=1 fixtures, 184 exact differential cases and 10 error probes passed. Native i32 compiler self-host passed; general dynamic compiler convergence remains separate. Independent root verification PASS: 735072 machine-text bytes, 13838 relocations and 468 symbols exact; all V++ modules and 11 same-source benchmark binaries exact; 16 GC=1 fixtures, 184 differential outcomes, C++ AOT and VM JIT off/on, 10 error probes passed.

| Case | CP30 ns/op | CP31 ns/op | Change |
|---|---:|---:|---:|
| common/json_parse | 150,478.516 | 150,679.688 | +0.13% |
| common/unicode_replace | 13,768.555 | 13,704.590 | -0.46% |
| common/arithmetic | 1.986 | 1.989 | +0.17% |
| common/branch_loop | 238.070 | 237.804 | -0.11% |
| common/fibonacci | 84.599 | 84.564 | -0.04% |
| common/list_128 | 158.161 | 157.681 | -0.30% |
| utf8/strict_mixed | 67,128.906 | 67,023.438 | -0.16% |
| utf8/strict_ascii | 3,095.703 | 3,119.141 | +0.76% |
| utf8/length_mixed | 80,093.750 | 79,980.469 | -0.14% |
| utf8/length_ascii | 14,808.594 | 14,806.641 | -0.01% |
| decimal/decimal | 28,058.838 | 28,039.795 | -0.07% |
| decimal/json_decimal | 102,531.249 | 102,562.500 | +0.03% |
| scan/scan | 62,750.001 | 62,687.499 | -0.10% |
| scan/ascii | 101,218.751 | 100,718.750 | -0.49% |
| scan/mixed | 488,968.750 | 489,843.750 | +0.18% |
| scan/dense | 23,237,750.000 | 23,567,375.000 | +1.42% |
| scan/sparse | 70,735,000.000 | 70,662,750.000 | -0.10% |
| scan/parse_ascii | 99,812.500 | 99,656.250 | -0.16% |
| scan/parse_mixed | 458,312.498 | 457,625.002 | -0.15% |
| extra/parse_nested | 7,390,749.990 | 7,415,250.002 | +0.33% |
| extra/parse_long | 110,250.004 | 112,499.997 | +2.04% |
| extra/parse_very_long | 903,499.990 | 900,000.006 | -0.39% |
| extra/parse_huge | 7,759,999.990 | 7,810,999.990 | +0.66% |
| extra/serialize_nested | 13,562,000.008 | 13,546,750.010 | -0.11% |
| extra/serialize_long | 2,605,624.996 | 2,610,749.998 | +0.20% |
| extra/serialize_very_long | 21,850,000.025 | 21,853,500.010 | +0.02% |
| extra/escape | 22,980,750.000 | 22,944,250.000 | -0.16% |
| extra/object | 374,656,000.000 | 373,722,750.000 | -0.25% |
| numbers/normal | 89,812.500 | 89,843.750 | +0.03% |
| numbers/overflow | 45,006,500.000 | 45,293,000.000 | +0.64% |
| numbers/long | 64,541,250.000 | 64,428,250.000 | -0.18% |
| keys/key_long | 819,749.996 | 819,250.003 | -0.06% |
| keys/key_mixed | 1,774,999.991 | 1,775,250.003 | +0.01% |
| keys/key_short | 490,531.251 | 485,906.251 | -0.94% |
| set/set_scan | 13,531.249 | 13,968.749 | +3.23% |
| set/space_parse | 48,437.502 | 48,187.502 | -0.52% |
| set/pretty | 107,250.001 | 108,624.999 | +1.28% |
| escape-parse/parse_dense | 24,459,125.000 | 24,572,750.000 | +0.46% |
| escape-parse/parse_sparse | 28,274,249.975 | 28,321,500.000 | +0.17% |
| escape-parse/parse_ascii | 100,000.000 | 101,593.750 | +1.59% |
| escape-parse/parse_mixed | 450,187.499 | 454,250.000 | +0.90% |
| prefix/decimal | 86,505.859 | 27,371.582 | -68.36% |


### CP31 cross-language comparison (macOS ARM64, 2026-11-13 local)

Five randomized rounds, three warmups, exact checksums and empty stderr. 200 samples; calibrated in-process batches; startup and compilation excluded. C and V++ i32 cover only numeric cases; no C++/Java JSON comparator, Rust unavailable.

| Runtime | Arithmetic ns/op | Branch ns/op | Fibonacci ns/op | List ns/op | Unicode ns/op | JSON ns/op |
|---|---:|---:|---:|---:|---:|---:|
| vpp-aot-vpp | 1.953 | 238.380 | 84.756 | 157.986 | 13,977.453 | 147,889.881 |
| vpp-i32 | 1.141 | 136.434 | 19.852 | — | — | — |
| c | 0.796 | 184.807 | 8.740 | — | — | — |
| cpp | 0.797 | 181.960 | 8.817 | 238.305 | 180.452 | — |
| node | 1.104 | 151.030 | 11.228 | 522.111 | 1,299.390 | 400.439 |
| java | 1.346 | 67.820 | 4.458 | 359.776 | 189.056 | — |
| python | 95.098 | 4,212.349 | 464.979 | 5,149.782 | 444.704 | 1,969.942 |
| ruby | 81.690 | 6,531.974 | 1,046.976 | 7,383.840 | 1,199.663 | 2,118.028 |


## CP32 — resume native i32 runs in mixed arrays (CP31 vs CP32)

42 prior cases and two mixed-array cases, five randomized paired rounds. 17 GC=1 fixtures, 184 prior and 318 mixed exact differential cases, 10 error probes, and C++ AOT plus VM JIT off/on passed. Runtime/compiler binaries unchanged from CP31; general dynamic compiler convergence remains separate. Independent root Stage0/runtime/general compiler rebuilt; runtime machine text, all V++ modules and 12 same-source workload binaries exact. All 17 GC=1 fixtures, 502 differential cases, 10 error probes and C++ AOT/VM JIT off/on passed; git diff --check passed.

| Case | CP31 ns/op | CP32 ns/op | Change |
|---|---:|---:|---:|
| common/json_parse | 141,912.109 | 147,441.406 | +3.90% |
| common/unicode_replace | 13,692.383 | 13,679.687 | -0.09% |
| common/arithmetic | 1.956 | 1.957 | +0.04% |
| common/branch_loop | 238.077 | 238.211 | +0.06% |
| common/fibonacci | 85.054 | 84.510 | -0.64% |
| common/list_128 | 157.955 | 158.367 | +0.26% |
| utf8/strict_mixed | 67,031.250 | 67,093.750 | +0.09% |
| utf8/strict_ascii | 3,000.000 | 3,033.203 | +1.11% |
| utf8/length_mixed | 77,966.797 | 76,570.312 | -1.79% |
| utf8/length_ascii | 14,248.047 | 14,351.562 | +0.73% |
| decimal/decimal | 27,416.992 | 27,384.521 | -0.12% |
| decimal/json_decimal | 94,249.999 | 95,062.500 | +0.86% |
| scan/scan | 61,250.001 | 61,937.502 | +1.12% |
| scan/ascii | 98,406.252 | 98,406.250 | -0.00% |
| scan/mixed | 472,281.251 | 479,312.500 | +1.49% |
| scan/dense | 21,891,625.000 | 22,644,125.000 | +3.44% |
| scan/sparse | 66,485,875.000 | 66,711,375.000 | +0.34% |
| scan/parse_ascii | 95,750.000 | 95,531.249 | -0.23% |
| scan/parse_mixed | 439,593.751 | 442,343.749 | +0.63% |
| extra/parse_nested | 7,327,500.000 | 7,548,000.007 | +3.01% |
| extra/parse_long | 112,750.001 | 112,750.001 | +0.00% |
| extra/parse_very_long | 912,499.994 | 909,999.997 | -0.27% |
| extra/parse_huge | 7,575,999.975 | 7,539,000.034 | -0.49% |
| extra/serialize_nested | 13,145,750.000 | 13,118,499.995 | -0.21% |
| extra/serialize_long | 2,550,124.995 | 2,544,124.999 | -0.24% |
| extra/serialize_very_long | 21,308,999.985 | 21,290,499.985 | -0.09% |
| extra/escape | 22,077,375.000 | 22,449,875.000 | +1.69% |
| extra/object | 366,019,750.000 | 364,539,500.000 | -0.40% |
| numbers/normal | 86,031.251 | 88,125.000 | +2.43% |
| numbers/overflow | 44,607,250.000 | 45,599,000.000 | +2.22% |
| numbers/long | 64,382,750.000 | 65,017,750.000 | +0.99% |
| keys/key_long | 818,625.003 | 839,374.997 | +2.53% |
| keys/key_mixed | 1,777,999.997 | 1,793,249.995 | +0.86% |
| keys/key_short | 486,187.501 | 487,718.750 | +0.31% |
| set/set_scan | 14,062.500 | 14,187.500 | +0.89% |
| set/space_parse | 54,125.000 | 54,687.500 | +1.04% |
| set/pretty | 118,687.499 | 118,312.500 | -0.32% |
| escape-parse/parse_dense | 24,316,875.000 | 24,324,750.000 | +0.03% |
| escape-parse/parse_sparse | 28,357,750.000 | 28,800,750.000 | +1.56% |
| escape-parse/parse_ascii | 98,937.500 | 99,156.251 | +0.22% |
| escape-parse/parse_mixed | 450,250.000 | 432,281.250 | -3.99% |
| prefix/decimal | 27,446.533 | 27,352.051 | -0.34% |
| mixed/mixed_run | 14,500,437.500 | 635,531.248 | -95.62% |
| mixed/mixed_large | 64,805,500.000 | 2,591,375.001 | -96.00% |


## CP33 — bounded single-pass literal replacement (CP32 vs CP33)

44 prior cases and seven replacement cases, five randomized paired rounds. 18 GC=1 fixtures (including 480 byte-oracle cases), 184 prior and 318 mixed exact differential cases, 10 error probes, and C++ AOT plus VM JIT off/on passed. V++ runtime changed; compiler binary unchanged from CP32; general dynamic compiler convergence remains separate. Independent root Stage0/runtime/general compiler rebuilt; runtime machine text, all V++ modules and 13 same-source workload binaries exact. All 18 GC=1 fixtures, 502 differential cases, 10 error probes and C++ AOT/VM JIT off/on passed; git diff --check passed.

| Case | CP32 ns/op | CP33 ns/op | Change |
|---|---:|---:|---:|
| common/json_parse | 147,552.734 | 147,523.437 | -0.02% |
| common/unicode_replace | 13,734.375 | 12,161.621 | -11.45% |
| common/arithmetic | 1.953 | 1.951 | -0.06% |
| common/branch_loop | 237.661 | 238.457 | +0.33% |
| common/fibonacci | 84.746 | 85.150 | +0.48% |
| common/list_128 | 158.360 | 158.188 | -0.11% |
| utf8/strict_mixed | 67,078.125 | 67,041.016 | -0.06% |
| utf8/strict_ascii | 3,037.109 | 3,029.297 | -0.26% |
| utf8/length_mixed | 78,304.688 | 78,314.453 | +0.01% |
| utf8/length_ascii | 14,384.766 | 14,406.250 | +0.15% |
| decimal/decimal | 27,421.631 | 27,481.445 | +0.22% |
| decimal/json_decimal | 100,187.501 | 100,812.500 | +0.62% |
| scan/scan | 61,531.249 | 61,343.750 | -0.30% |
| scan/ascii | 99,531.250 | 99,343.749 | -0.19% |
| scan/mixed | 478,968.751 | 484,531.250 | +1.16% |
| scan/dense | 23,194,875.000 | 23,071,125.000 | -0.53% |
| scan/sparse | 69,751,000.000 | 69,472,125.000 | -0.40% |
| scan/parse_ascii | 97,718.749 | 97,500.000 | -0.22% |
| scan/parse_mixed | 449,437.499 | 448,281.249 | -0.26% |
| extra/parse_nested | 7,571,750.000 | 7,542,250.008 | -0.39% |
| extra/parse_long | 110,374.995 | 110,249.996 | -0.11% |
| extra/parse_very_long | 906,999.975 | 902,999.997 | -0.44% |
| extra/parse_huge | 7,787,000.000 | 7,782,000.005 | -0.06% |
| extra/serialize_nested | 13,553,000.002 | 13,595,000.000 | +0.31% |
| extra/serialize_long | 2,602,875.001 | 2,603,874.996 | +0.04% |
| extra/serialize_very_long | 21,838,500.025 | 21,898,499.995 | +0.27% |
| extra/escape | 22,966,750.000 | 22,921,750.000 | -0.20% |
| extra/object | 374,582,125.000 | 377,595,000.000 | +0.80% |
| numbers/normal | 89,875.000 | 89,531.248 | -0.38% |
| numbers/overflow | 45,312,000.000 | 45,415,250.000 | +0.23% |
| numbers/long | 65,055,250.000 | 64,883,500.000 | -0.26% |
| keys/key_long | 821,375.005 | 822,375.000 | +0.12% |
| keys/key_mixed | 1,775,500.000 | 1,774,749.994 | -0.04% |
| keys/key_short | 497,812.500 | 497,312.499 | -0.10% |
| set/set_scan | 17,874.999 | 18,718.751 | +4.72% |
| set/space_parse | 52,437.499 | 52,375.000 | -0.12% |
| set/pretty | 118,687.499 | 116,687.499 | -1.69% |
| escape-parse/parse_dense | 24,763,625.000 | 24,353,750.000 | -1.66% |
| escape-parse/parse_sparse | 28,291,250.000 | 28,697,500.000 | +1.44% |
| escape-parse/parse_ascii | 97,999.999 | 98,437.499 | +0.45% |
| escape-parse/parse_mixed | 450,187.499 | 448,968.749 | -0.27% |
| prefix/decimal | 27,461.426 | 27,417.236 | -0.16% |
| mixed/mixed_run | 650,812.501 | 650,468.750 | -0.05% |
| mixed/mixed_large | 2,658,624.999 | 2,668,375.000 | +0.37% |
| replace/shrink_short | 12,457.031 | 10,393.555 | -16.56% |
| replace/equal | 13,523.926 | 10,393.555 | -23.15% |
| replace/grow | 15,639.160 | 15,631.348 | -0.05% |
| replace/delete | 10,380.371 | 7,403.809 | -28.67% |
| replace/shrink_long | 182,500.000 | 184,507.813 | +1.10% |
| replace/no_match | 3,684.082 | 3,679.688 | -0.12% |
| replace/sparse | 25,824.219 | 16,691.406 | -35.37% |


## CP34 — early exit in stable list sort (CP33 vs CP34)

51 prior cases and five sort cases, five randomized paired rounds. 19 GC=1 fixtures (including 480 byte-oracle cases and 512-key JSON roundtrip), 184 prior and 318 mixed exact differential cases, 10 error probes, and C++ AOT plus VM JIT off/on passed. V++ list module changed; runtime and compiler binaries unchanged from CP33; general dynamic compiler convergence remains separate. Existing native map-delete return-value mismatch reproduced on CP33 and CP34 in two collection fixtures, tracked separately. Initial target/sort measurements used identical binaries due to source-tree import resolution and were discarded; corrected baseline built from CP33. Initial length_ascii (+7.83%) and space_parse (+7.29%) outliers were retested once with 8x/16x iterations, five paired rounds (+1.39%/+0.44%); initial samples retained. Independent root Stage0/runtime/general compiler rebuilt; runtime machine text, all V++ modules and 14 same-source workload binaries exact. All 19 GC=1 fixtures, 502 differential cases, 10 error probes and C++ AOT/VM JIT off/on passed; git diff --check passed.

| Case | CP33 ns/op | CP34 ns/op | Change |
|---|---:|---:|---:|
| common/json_parse | 152,181.641 | 153,869.141 | +1.11% |
| common/unicode_replace | 12,550.781 | 12,620.605 | +0.56% |
| common/arithmetic | 2.058 | 2.034 | -1.15% |
| common/branch_loop | 243.958 | 248.640 | +1.92% |
| common/fibonacci | 91.717 | 88.037 | -4.01% |
| common/list_128 | 167.934 | 163.857 | -2.43% |
| utf8/strict_mixed | 68,923.828 | 68,625.000 | -0.43% |
| utf8/strict_ascii | 3,062.500 | 3,093.750 | +1.02% |
| utf8/length_mixed | 82,312.500 | 82,068.359 | -0.30% |
| utf8/length_ascii | 14,599.854 | 14,802.246 | +1.39% |
| decimal/decimal | 30,478.516 | 29,874.268 | -1.98% |
| decimal/json_decimal | 106,750.000 | 105,656.249 | -1.02% |
| scan/scan | 62,406.251 | 63,437.499 | +1.65% |
| scan/ascii | 103,187.501 | 103,437.500 | +0.24% |
| scan/mixed | 508,531.250 | 507,906.249 | -0.12% |
| scan/dense | 23,649,875.000 | 23,830,000.000 | +0.76% |
| scan/sparse | 71,727,375.000 | 71,593,500.000 | -0.19% |
| scan/parse_ascii | 102,093.751 | 100,187.501 | -1.87% |
| scan/parse_mixed | 462,312.501 | 461,375.000 | -0.20% |
| extra/parse_nested | 7,722,499.997 | 7,693,250.000 | -0.38% |
| extra/parse_long | 111,250.006 | 114,624.999 | +3.03% |
| extra/parse_very_long | 934,500.009 | 913,499.981 | -2.25% |
| extra/parse_huge | 8,087,000.012 | 8,114,000.022 | +0.33% |
| extra/serialize_nested | 14,116,750.002 | 13,759,999.990 | -2.53% |
| extra/serialize_long | 3,453,750.006 | 3,400,250.002 | -1.55% |
| extra/serialize_very_long | 24,101,000.010 | 25,278,999.985 | +4.89% |
| extra/escape | 24,205,750.000 | 23,762,625.000 | -1.83% |
| extra/object | 387,961,250.000 | 41,614,625.000 | -89.27% |
| numbers/normal | 92,343.749 | 92,000.000 | -0.37% |
| numbers/overflow | 47,591,500.000 | 47,067,500.000 | -1.10% |
| numbers/long | 66,664,000.000 | 67,448,999.975 | +1.18% |
| keys/key_long | 841,250.002 | 839,374.997 | -0.22% |
| keys/key_mixed | 1,810,749.993 | 1,807,750.002 | -0.17% |
| keys/key_short | 500,218.749 | 494,812.500 | -1.08% |
| set/set_scan | 14,093.751 | 14,593.750 | +3.55% |
| set/space_parse | 47,257.812 | 47,464.844 | +0.44% |
| set/pretty | 115,375.001 | 116,687.503 | +1.14% |
| escape-parse/parse_dense | 25,260,250.000 | 25,413,125.000 | +0.61% |
| escape-parse/parse_sparse | 29,729,000.000 | 29,959,750.000 | +0.78% |
| escape-parse/parse_ascii | 102,718.750 | 101,249.998 | -1.43% |
| escape-parse/parse_mixed | 477,000.000 | 462,562.500 | -3.03% |
| prefix/decimal | 36,095.215 | 29,573.486 | -18.07% |
| mixed/mixed_run | 663,937.500 | 672,562.500 | +1.30% |
| mixed/mixed_large | 2,746,375.003 | 2,729,000.002 | -0.63% |
| replace/shrink_short | 10,567.383 | 10,599.121 | +0.30% |
| replace/equal | 10,513.672 | 10,533.203 | +0.19% |
| replace/grow | 15,852.051 | 15,684.570 | -1.06% |
| replace/delete | 7,529.785 | 7,895.020 | +4.85% |
| replace/shrink_long | 186,601.562 | 187,007.812 | +0.22% |
| replace/no_match | 3,699.707 | 3,708.008 | +0.22% |
| replace/sparse | 17,035.156 | 17,164.062 | +0.76% |
| sort/sorted | 21,410,125.000 | 332,671.875 | -98.45% |
| sort/sorted_large | 343,182,312.500 | 1,310,250.003 | -99.62% |
| sort/reverse | 40,832,437.500 | 41,583,187.500 | +1.84% |
| sort/random | 30,714,125.000 | 30,814,500.000 | +0.33% |
| sort/nearly | 20,918,468.750 | 648,765.625 | -96.90% |


## CP35 — map remove returns prior value (CP34 vs CP35)

56 prior cases and four map workloads, five randomized paired rounds. 22 GC=1 fixtures, including the original list literal and collections OOP tests without expected-output changes; 184 prior and 318 mixed exact differential cases, 10 error probes, and C++ AOT plus VM JIT off/on passed. V++ host opcode 151 now returns the removed value (or null for an absent key), uses one overlapping pair move and clears trailing GC references. General compiler unchanged. Independent root Stage0/runtime/general compiler rebuilt; runtime machine text, all V++ modules and 15 same-source workload binaries exact. All 22 GC=1 fixtures, 502 differential cases, 10 error probes and C++ AOT/VM JIT off/on passed; git diff --check passed.

| Case | CP34 ns/op | CP35 ns/op | Change |
|---|---:|---:|---:|
| common/json_parse | 154,658.203 | 155,144.531 | +0.31% |
| common/unicode_replace | 12,552.734 | 12,671.875 | +0.95% |
| common/arithmetic | 2.062 | 2.050 | -0.56% |
| common/branch_loop | 256.432 | 247.879 | -3.34% |
| common/fibonacci | 92.079 | 91.584 | -0.54% |
| common/list_128 | 171.307 | 171.585 | +0.16% |
| utf8/strict_mixed | 70,810.547 | 70,109.375 | -0.99% |
| utf8/strict_ascii | 3,173.828 | 3,171.875 | -0.06% |
| utf8/length_mixed | 82,654.297 | 82,439.453 | -0.26% |
| utf8/length_ascii | 15,351.562 | 15,333.984 | -0.11% |
| decimal/decimal | 29,429.932 | 29,577.881 | +0.50% |
| decimal/json_decimal | 109,406.250 | 107,968.749 | -1.31% |
| scan/scan | 63,968.752 | 63,406.249 | -0.88% |
| scan/ascii | 103,875.000 | 102,562.500 | -1.26% |
| scan/mixed | 492,000.001 | 492,843.749 | +0.17% |
| scan/dense | 24,733,500.000 | 25,006,625.000 | +1.10% |
| scan/sparse | 75,439,750.000 | 75,350,000.000 | -0.12% |
| scan/parse_ascii | 104,437.500 | 105,781.250 | +1.29% |
| scan/parse_mixed | 477,375.001 | 488,968.750 | +2.43% |
| extra/parse_nested | 7,955,749.987 | 8,068,000.002 | +1.41% |
| extra/parse_long | 116,374.999 | 116,250.001 | -0.11% |
| extra/parse_very_long | 919,000.000 | 921,000.004 | +0.22% |
| extra/parse_huge | 8,136,000.037 | 8,084,999.979 | -0.63% |
| extra/serialize_nested | 13,480,749.995 | 13,853,750.005 | +2.77% |
| extra/serialize_long | 2,741,375.006 | 2,733,624.995 | -0.28% |
| extra/serialize_very_long | 23,302,500.010 | 23,463,499.995 | +0.69% |
| extra/escape | 24,336,875.000 | 24,335,625.000 | -0.01% |
| extra/object | 42,378,000.000 | 42,549,750.000 | +0.41% |
| numbers/normal | 93,250.001 | 93,437.500 | +0.20% |
| numbers/overflow | 48,098,500.000 | 48,040,250.000 | -0.12% |
| numbers/long | 68,794,000.000 | 71,814,250.000 | +4.39% |
| keys/key_long | 848,749.995 | 844,374.999 | -0.52% |
| keys/key_mixed | 1,841,250.002 | 1,841,250.002 | +0.00% |
| keys/key_short | 521,343.749 | 522,406.250 | +0.20% |
| set/set_scan | 15,343.750 | 15,125.001 | -1.43% |
| set/space_parse | 54,937.501 | 57,437.502 | +4.55% |
| set/pretty | 121,437.497 | 120,687.500 | -0.62% |
| escape-parse/parse_dense | 26,502,250.000 | 25,990,125.000 | -1.93% |
| escape-parse/parse_sparse | 30,126,250.000 | 30,065,250.000 | -0.20% |
| escape-parse/parse_ascii | 103,500.001 | 102,750.000 | -0.72% |
| escape-parse/parse_mixed | 486,125.000 | 488,125.000 | +0.41% |
| prefix/decimal | 29,349.854 | 28,696.533 | -2.23% |
| mixed/mixed_run | 679,687.500 | 688,468.749 | +1.29% |
| mixed/mixed_large | 2,798,874.996 | 2,792,000.004 | -0.25% |
| replace/shrink_short | 10,696.777 | 11,008.789 | +2.92% |
| replace/equal | 10,854.980 | 10,840.820 | -0.13% |
| replace/grow | 16,110.352 | 16,386.230 | +1.71% |
| replace/delete | 7,681.641 | 7,718.750 | +0.48% |
| replace/shrink_long | 195,296.875 | 193,398.438 | -0.97% |
| replace/no_match | 3,948.730 | 3,864.746 | -2.13% |
| replace/sparse | 17,910.156 | 17,890.625 | -0.11% |
| sort/sorted | 352,328.125 | 346,109.374 | -1.77% |
| sort/sorted_large | 1,346,499.998 | 1,377,062.499 | +2.27% |
| sort/reverse | 42,156,625.000 | 41,762,375.000 | -0.94% |
| sort/random | 30,941,125.000 | 30,864,750.000 | -0.25% |
| sort/nearly | 672,593.750 | 669,265.625 | -0.49% |
| target/delete | 4,504,874.996 | 4,374,375.000 | -2.90% |
| target/insert | 1,383,000.001 | 1,365,625.001 | -1.26% |
| target/lookup | 1,677,332.031 | 1,683,839.844 | +0.39% |
| target/update | 2,857,007.812 | 2,864,570.312 | +0.26% |


## CP36 — single lookup per JSON object member (CP35 vs CP36)

All 60 prior workloads and two object serialization targets, five randomized paired rounds. 23 GC=1 fixtures, including null, Unicode, object size boundaries, missing-key defaults and cycle handling; 184 prior and 318 mixed exact differential cases, 10 error probes, and C++ AOT plus VM JIT off/on passed. JSON serialization uses direct map indexing for keys enumerated from that map, avoiding redundant membership checks; the public default lookup is unchanged. Runtime and compiler unchanged. Independent root Stage0/runtime/general compiler rebuilt; runtime machine text, all V++ modules and 16 same-source workload binaries exact. All 23 GC=1 fixtures, 502 differential cases, 10 error probes and C++ AOT/VM JIT off/on passed; git diff --check passed.

| Case | CP35 ns/op | CP36 ns/op | Change |
|---|---:|---:|---:|
| common/json_parse | 154,988.281 | 155,757.812 | +0.50% |
| common/unicode_replace | 12,444.336 | 12,583.496 | +1.12% |
| common/arithmetic | 2.026 | 2.025 | -0.08% |
| common/branch_loop | 246.820 | 248.320 | +0.61% |
| common/fibonacci | 88.853 | 87.550 | -1.47% |
| common/list_128 | 166.172 | 163.540 | -1.58% |
| utf8/strict_mixed | 69,287.109 | 68,878.906 | -0.59% |
| utf8/strict_ascii | 3,125.000 | 3,115.234 | -0.31% |
| utf8/length_mixed | 86,431.641 | 86,097.656 | -0.39% |
| utf8/length_ascii | 16,259.766 | 16,089.844 | -1.05% |
| decimal/decimal | 28,105.713 | 27,990.234 | -0.41% |
| decimal/json_decimal | 103,500.001 | 103,093.751 | -0.39% |
| scan/scan | 64,250.002 | 62,281.251 | -3.06% |
| scan/ascii | 102,406.250 | 102,750.000 | +0.34% |
| scan/mixed | 493,281.251 | 489,593.750 | -0.75% |
| scan/dense | 23,744,750.000 | 23,813,250.000 | +0.29% |
| scan/sparse | 72,879,500.000 | 73,235,125.000 | +0.49% |
| scan/parse_ascii | 100,718.750 | 100,843.750 | +0.12% |
| scan/parse_mixed | 462,812.500 | 470,999.999 | +1.77% |
| extra/parse_nested | 7,818,000.002 | 7,832,499.995 | +0.19% |
| extra/parse_long | 115,249.999 | 115,500.003 | +0.22% |
| extra/parse_very_long | 927,000.016 | 939,999.997 | +1.40% |
| extra/parse_huge | 8,109,000.027 | 8,152,000.010 | +0.53% |
| extra/serialize_nested | 13,807,250.008 | 13,351,250.007 | -3.30% |
| extra/serialize_long | 2,822,625.004 | 2,804,375.000 | -0.65% |
| extra/serialize_very_long | 23,777,999.995 | 23,586,500.020 | -0.81% |
| extra/escape | 24,463,625.000 | 24,264,375.000 | -0.81% |
| extra/object | 42,851,875.000 | 40,610,250.000 | -5.23% |
| numbers/normal | 95,125.001 | 92,468.750 | -2.79% |
| numbers/overflow | 48,686,750.000 | 48,797,250.000 | +0.23% |
| numbers/long | 69,172,750.000 | 67,968,250.000 | -1.74% |
| keys/key_long | 843,750.000 | 861,625.001 | +2.12% |
| keys/key_mixed | 1,838,000.000 | 1,869,250.000 | +1.70% |
| keys/key_short | 507,781.250 | 510,968.750 | +0.63% |
| set/set_scan | 14,937.500 | 14,750.000 | -1.26% |
| set/space_parse | 53,312.499 | 53,499.997 | +0.35% |
| set/pretty | 118,812.501 | 120,875.001 | +1.74% |
| escape-parse/parse_dense | 25,684,125.000 | 25,816,500.000 | +0.52% |
| escape-parse/parse_sparse | 29,799,750.000 | 29,790,750.000 | -0.03% |
| escape-parse/parse_ascii | 105,687.499 | 103,875.000 | -1.71% |
| escape-parse/parse_mixed | 473,500.000 | 474,312.501 | +0.17% |
| prefix/decimal | 28,505.859 | 28,653.564 | +0.52% |
| mixed/mixed_run | 672,000.000 | 676,031.249 | +0.60% |
| mixed/mixed_large | 2,796,374.999 | 2,753,624.997 | -1.53% |
| replace/shrink_short | 10,766.113 | 10,768.066 | +0.02% |
| replace/equal | 11,047.363 | 10,756.836 | -2.63% |
| replace/grow | 15,832.031 | 16,296.387 | +2.93% |
| replace/delete | 7,577.148 | 7,500.488 | -1.01% |
| replace/shrink_long | 188,265.625 | 187,796.875 | -0.25% |
| replace/no_match | 3,775.391 | 3,842.285 | +1.77% |
| replace/sparse | 17,382.812 | 16,992.187 | -2.25% |
| sort/sorted | 355,062.500 | 367,203.125 | +3.42% |
| sort/sorted_large | 1,334,625.002 | 1,324,812.498 | -0.74% |
| sort/reverse | 42,015,875.000 | 41,742,875.000 | -0.65% |
| sort/random | 31,076,437.500 | 31,114,625.000 | +0.12% |
| sort/nearly | 684,093.750 | 664,968.750 | -2.80% |
| map/delete | 4,458,624.996 | 4,419,874.996 | -0.87% |
| map/insert | 1,367,750.004 | 1,397,250.004 | +2.16% |
| map/lookup | 1,718,964.844 | 1,728,789.062 | +0.57% |
| map/update | 2,998,812.500 | 2,944,187.500 | -1.82% |
| target/small_object | 265,958.984 | 256,019.531 | -3.74% |
| target/boundary_object | 10,163,687.500 | 9,753,375.000 | -4.04% |


## CP37 — stable merge sort for large homogeneous lists (CP36 vs CP37)

All 62 prior workloads and three large sorting targets, five randomized paired rounds. 24 GC=1 fixtures, including boundary lengths, aliases, Unicode and malformed bytes, mixed types, tuple and NaN fallback; 184 prior and 318 mixed exact differential cases, 10 prior error probes and two additional sorting error probes, and C++ AOT plus VM JIT off/on passed. Two initial bubble passes preserve early exits; larger homogeneous integer/string lists use iterative stable merge sort. Other inputs retain the existing comparison and error order. Runtime and compiler unchanged. The initial eager type guard regressed nearly-sorted input by 96.96%; all 65 initial results are retained under rejected-eager-guard. Moving the expensive guard into a nested conditional fixes eager && evaluation. The corrected candidate was rebuilt and all correctness and performance checks rerun. Sorting errors match the baseline exactly on native AOT; VM diagnostics match after normalizing only source locations, retaining error text and stack function order. Independent root Stage0/runtime/general compiler rebuilt; runtime machine text, all V++ modules and 17 same-source workload binaries exact. All 24 GC=1 fixtures, 502 differential cases, 12 error probes and C++ AOT/VM JIT off/on passed; git diff --check passed.

| Case | CP36 ns/op | CP37 ns/op | Change |
|---|---:|---:|---:|
| common/json_parse | 152,644.531 | 154,921.875 | +1.49% |
| common/unicode_replace | 12,527.344 | 12,697.754 | +1.36% |
| common/arithmetic | 2.040 | 2.030 | -0.51% |
| common/branch_loop | 250.196 | 251.274 | +0.43% |
| common/fibonacci | 88.877 | 89.038 | +0.18% |
| common/list_128 | 165.611 | 162.945 | -1.61% |
| utf8/strict_mixed | 70,822.266 | 70,158.203 | -0.94% |
| utf8/strict_ascii | 3,123.047 | 3,183.594 | +1.94% |
| utf8/length_mixed | 81,724.609 | 80,042.969 | -2.06% |
| utf8/length_ascii | 15,287.109 | 15,492.188 | +1.34% |
| decimal/decimal | 28,707.520 | 28,982.666 | +0.96% |
| decimal/json_decimal | 106,000.001 | 105,125.001 | -0.83% |
| scan/scan | 64,406.252 | 63,937.500 | -0.73% |
| scan/ascii | 102,718.750 | 104,562.499 | +1.79% |
| scan/mixed | 489,593.750 | 500,875.002 | +2.30% |
| scan/dense | 24,188,250.000 | 24,257,500.000 | +0.29% |
| scan/sparse | 72,706,000.000 | 72,809,875.000 | +0.14% |
| scan/parse_ascii | 104,781.251 | 103,156.250 | -1.55% |
| scan/parse_mixed | 466,281.250 | 469,968.749 | +0.79% |
| extra/parse_nested | 7,924,500.003 | 7,785,750.003 | -1.75% |
| extra/parse_long | 116,875.000 | 113,124.996 | -3.21% |
| extra/parse_very_long | 930,500.001 | 914,499.998 | -1.72% |
| extra/parse_huge | 8,430,999.994 | 8,217,000.008 | -2.54% |
| extra/serialize_nested | 13,067,000.002 | 12,937,249.987 | -0.99% |
| extra/serialize_long | 2,724,749.998 | 2,731,124.997 | +0.23% |
| extra/serialize_very_long | 22,942,000.000 | 22,844,500.005 | -0.42% |
| extra/escape | 24,347,125.000 | 24,392,750.000 | +0.19% |
| extra/object | 40,996,625.000 | 40,522,125.000 | -1.16% |
| numbers/normal | 92,468.750 | 94,156.250 | +1.82% |
| numbers/overflow | 47,950,250.000 | 47,781,000.000 | -0.35% |
| numbers/long | 68,602,500.000 | 68,884,000.000 | +0.41% |
| keys/key_long | 855,250.001 | 872,874.997 | +2.06% |
| keys/key_mixed | 1,864,749.998 | 1,838,500.008 | -1.41% |
| keys/key_short | 522,562.500 | 513,406.251 | -1.75% |
| set/set_scan | 14,750.000 | 14,750.000 | +0.00% |
| set/space_parse | 54,249.998 | 53,937.502 | -0.58% |
| set/pretty | 120,249.998 | 120,625.000 | +0.31% |
| escape-parse/parse_dense | 25,929,875.000 | 25,644,125.000 | -1.10% |
| escape-parse/parse_sparse | 29,823,250.000 | 30,159,500.000 | +1.13% |
| escape-parse/parse_ascii | 104,937.501 | 104,468.750 | -0.45% |
| escape-parse/parse_mixed | 474,250.000 | 476,718.750 | +0.52% |
| prefix/decimal | 29,044.922 | 28,876.465 | -0.58% |
| mixed/mixed_run | 680,218.751 | 674,750.000 | -0.80% |
| mixed/mixed_large | 2,802,374.996 | 2,781,624.995 | -0.74% |
| replace/shrink_short | 10,901.367 | 10,855.957 | -0.42% |
| replace/equal | 10,812.500 | 10,874.023 | +0.57% |
| replace/grow | 16,144.043 | 16,150.879 | +0.04% |
| replace/delete | 7,650.879 | 7,615.234 | -0.47% |
| replace/shrink_long | 190,671.875 | 192,085.938 | +0.74% |
| replace/no_match | 3,872.070 | 3,875.977 | +0.10% |
| replace/sparse | 17,726.562 | 17,710.938 | -0.09% |
| sort/sorted | 347,750.000 | 346,703.125 | -0.30% |
| sort/sorted_large | 1,334,249.999 | 1,347,124.997 | +0.96% |
| sort/reverse | 42,675,562.500 | 5,809,000.001 | -86.39% |
| sort/random | 31,901,062.500 | 6,274,250.000 | -80.33% |
| sort/nearly | 660,031.250 | 685,265.625 | +3.82% |
| map/delete | 4,728,875.004 | 4,435,625.001 | -6.20% |
| map/insert | 1,609,625.004 | 1,652,249.999 | +2.65% |
| map/lookup | 1,680,273.438 | 1,688,726.562 | +0.50% |
| map/update | 2,910,609.375 | 2,933,632.812 | +0.79% |
| target/small_object | 254,707.031 | 258,429.688 | +1.46% |
| target/boundary_object | 9,683,093.750 | 9,634,468.750 | -0.50% |
| merge/reverse_large | 673,978,000.000 | 25,596,500.010 | -96.20% |
| merge/random_large | 498,050,000.000 | 27,347,000.005 | -94.51% |
| merge/unicode_large | 479,842,500.000 | 27,289,500.000 | -94.31% |

### CP37 cross-language benchmark refresh

CP37 cross-language refresh (macOS ARM64, 5 randomized rounds, 200 verified samples; ns/op; i32 is the retained older restricted executable). Compilation/startup excluded. Missing comparators are not estimated.

| Runtime | arithmetic | branch_loop | fibonacci | list_128 | unicode_replace | json_parse |
|---|---:|---:|---:|---:|---:|---:|
| vpp-aot-vpp | 2.059 | 251.697 | 88.844 | 167.396 | 13,122.768 | 156,371.710 |
| vpp-i32 | 1.214 | 154.981 | 20.855 | — | — | — |
| c | 0.834 | 206.980 | 9.341 | — | — | — |
| cpp | 0.842 | 205.512 | 9.530 | 250.797 | 195.909 | — |
| node | 1.151 | 156.484 | 11.771 | 562.952 | 1,404.767 | 433.729 |
| java | 1.415 | 71.102 | 4.672 | 429.218 | 200.395 | — |
| python | 101.845 | 4,385.601 | 486.089 | 5,347.262 | 475.958 | 2,121.548 |
| ruby | 82.340 | 6,655.789 | 1,106.554 | 7,792.061 | 1,298.600 | 2,294.336 |

| General V++ AOT | CP36 ns/op | CP37 ns/op | Change |
|---|---:|---:|---:|
| arithmetic | 2.036 | 2.059 | +1.10% |
| branch_loop | 247.852 | 251.697 | +1.55% |
| fibonacci | 88.215 | 88.844 | +0.71% |
| list_128 | 166.132 | 167.396 | +0.76% |
| unicode_replace | 13,355.494 | 13,122.768 | -1.74% |
| json_parse | 154,819.079 | 156,371.710 | +1.00% |

Cross-refresh differences are descriptive, not paired regression gates. Sorting improvements are measured separately in the 65-case CP36/CP37 paired A/B suite.


## CP38 — ARM64 register forwarding and leaf memory primitives (CP37 vs CP38)

The restricted self-host compiler retains single-use evaluation results in W9/W0 until the next store, binary operation or return. Branch targets are marked in a linear prepass using the existing offset table, so a direct branch cannot skip a necessary spill. macOS ARM64 memory primitives use a private leaf fast path with the original framed ABI preserved for other callers. No JSON-specific or Unicode-specific shortcuts, and no new production C++ dependency. Normalized self-host Mach-O convergence, exact machine equivalence to the initial faster candidate, native codegen encodings, signed i32/call/control-flow probes, memory bounds and framed calls, cross-assembly, 24 GC=1 fixtures, 502 differential cases, 12 error probes, and compatibility checks passed. All 65 paired workloads passed the regression gate; JSON and Unicode each improved by at least 20%. Root verification pending.

| Case | CP37 ns/op | CP38 ns/op | Change |
|---|---:|---:|---:|
| common/json_parse | 155,601.563 | 105,978.516 | -31.89% |
| common/unicode_replace | 12,662.598 | 8,517.578 | -32.73% |
| common/arithmetic | 2.056 | 2.017 | -1.87% |
| common/branch_loop | 250.404 | 250.603 | +0.08% |
| common/fibonacci | 88.834 | 89.214 | +0.43% |
| common/list_128 | 166.988 | 167.763 | +0.46% |
| utf8/strict_mixed | 69,416.016 | 49,187.500 | -29.14% |
| utf8/strict_ascii | 3,138.672 | 2,134.766 | -31.99% |
| utf8/length_mixed | 81,363.281 | 56,466.797 | -30.60% |
| utf8/length_ascii | 15,062.500 | 9,582.031 | -36.38% |
| decimal/decimal | 28,578.125 | 19,161.865 | -32.95% |
| decimal/json_decimal | 106,250.001 | 70,531.251 | -33.62% |
| scan/scan | 63,062.498 | 47,031.250 | -25.42% |
| scan/ascii | 102,156.250 | 71,343.750 | -30.16% |
| scan/mixed | 498,062.501 | 346,624.998 | -30.41% |
| scan/dense | 23,960,000.000 | 16,057,875.000 | -32.98% |
| scan/sparse | 73,124,375.000 | 49,283,750.000 | -32.60% |
| scan/parse_ascii | 169,156.250 | 101,218.751 | -40.16% |
| scan/parse_mixed | 698,187.500 | 454,343.749 | -34.93% |
| extra/parse_nested | 8,254,000.008 | 5,536,249.995 | -32.93% |
| extra/parse_long | 119,125.001 | 81,999.995 | -31.16% |
| extra/parse_very_long | 939,999.997 | 636,500.001 | -32.29% |
| extra/parse_huge | 8,169,000.030 | 5,362,999.976 | -34.35% |
| extra/serialize_nested | 13,113,000.005 | 8,697,750.002 | -33.67% |
| extra/serialize_long | 2,720,500.000 | 1,803,250.000 | -33.72% |
| extra/serialize_very_long | 22,718,999.980 | 15,053,999.990 | -33.74% |
| extra/escape | 23,939,000.000 | 15,848,125.000 | -33.80% |
| extra/object | 40,029,750.000 | 26,657,875.000 | -33.40% |
| numbers/normal | 96,562.501 | 63,843.749 | -33.88% |
| numbers/overflow | 47,810,500.000 | 31,662,500.000 | -33.78% |
| numbers/long | 67,015,500.000 | 44,468,500.000 | -33.64% |
| keys/key_long | 861,249.998 | 589,000.002 | -31.61% |
| keys/key_mixed | 1,879,249.990 | 1,272,000.000 | -32.31% |
| keys/key_short | 508,687.500 | 342,812.501 | -32.61% |
| set/set_scan | 14,500.000 | 10,187.499 | -29.74% |
| set/space_parse | 53,937.498 | 37,124.999 | -31.17% |
| set/pretty | 117,750.000 | 80,499.999 | -31.63% |
| escape-parse/parse_dense | 25,105,000.000 | 16,726,000.000 | -33.38% |
| escape-parse/parse_sparse | 29,261,500.000 | 18,919,750.005 | -35.34% |
| escape-parse/parse_ascii | 100,656.249 | 71,468.750 | -29.00% |
| escape-parse/parse_mixed | 467,062.499 | 322,343.750 | -30.98% |
| prefix/decimal | 28,318.359 | 19,114.502 | -32.50% |
| mixed/mixed_run | 668,437.500 | 445,218.749 | -33.39% |
| mixed/mixed_large | 2,727,999.993 | 1,819,375.001 | -33.31% |
| replace/shrink_short | 10,570.312 | 7,010.742 | -33.68% |
| replace/equal | 10,587.891 | 7,018.555 | -33.71% |
| replace/grow | 15,892.578 | 10,352.539 | -34.86% |
| replace/delete | 7,430.176 | 5,026.855 | -32.35% |
| replace/shrink_long | 189,101.563 | 121,437.500 | -35.78% |
| replace/no_match | 3,791.504 | 2,486.328 | -34.42% |
| replace/sparse | 17,320.312 | 10,925.781 | -36.92% |
| sort/sorted | 339,953.125 | 228,203.125 | -32.87% |
| sort/sorted_large | 1,306,312.501 | 868,999.999 | -33.48% |
| sort/reverse | 5,619,249.999 | 3,780,437.499 | -32.72% |
| sort/random | 5,991,999.999 | 4,021,437.499 | -32.89% |
| sort/nearly | 663,203.125 | 445,328.125 | -32.85% |
| map/delete | 4,415,375.001 | 2,895,000.004 | -34.43% |
| map/insert | 1,358,874.999 | 889,874.995 | -34.51% |
| map/lookup | 1,724,832.031 | 1,110,800.781 | -35.60% |
| map/update | 2,980,867.188 | 1,895,554.688 | -36.41% |
| target/small_object | 258,099.609 | 172,410.156 | -33.20% |
| target/boundary_object | 9,918,125.000 | 6,580,968.750 | -33.65% |
| merge/reverse_large | 25,902,500.005 | 17,345,499.990 | -33.04% |
| merge/random_large | 27,953,999.995 | 18,679,500.015 | -33.18% |
| merge/unicode_large | 28,058,999.985 | 18,819,000.005 | -32.93% |
