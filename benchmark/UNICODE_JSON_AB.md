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
