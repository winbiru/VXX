# V++ cross-language benchmark coverage

The baseline application benchmarks live in `benchmark/languages/workloads.vi`
and `scripts/quality/run_portable_cross_os_bench.py`. The independent bulk-data
extension lives in `benchmark/languages/bulk_io.*` and is driven by
`scripts/quality/benchmark_bulk_io.py`.

## Run

```sh
# Build the V++ CLI once before benchmarking.
python3 scripts/quality/benchmark_bulk_io.py --smoke --output-dir build-benchmark-bulk-smoke

# Opt-in, larger cached reads and repeated independent samples:
python3 scripts/quality/benchmark_bulk_io.py \
  --vpp-cli build/bin/vpp-cli --sizes-kib 16,256 --runs 5 \
  --output-dir build-benchmark-bulk

# Restrict to storage operations and selected runtimes:
python3 scripts/quality/benchmark_bulk_io.py \
  --cases read_bytes,write_bytes,stream_64k,bytes_sum,csv_split,jsonl_parse \
  --runtimes vpp-vm,python,cpp,node --sizes-kib 16 --runs 5 \
  --output-dir build-benchmark-storage

# Bigger data sets only on demand. The V++ VM can take much longer on 10 MiB.
python3 scripts/quality/benchmark_bulk_io.py \
  --sizes-kib 1024,10240 --runs 3 --timeout-s 240 \
  --output-dir build-benchmark-bulk-large
```

The script creates immutable input fixtures for the duration of the run and
stores hashes, source hashes, runtime versions, Git revision/dirty state, raw
sample timings, median, p95 latency, median absolute deviation (MAD), and
decimal MB/s in `results.json`. `mbps_at_p95_latency` converts p95 latency to
throughput; it does not mean 95th-percentile *highest* throughput.
`summary.md` is a compact comparison table; `comparisons.md` compares V++ VM
and Python median execution times for the same workload on the same host.
An oracle failure is fatal for
that cell, written under `errors`, and makes the runner exit nonzero.
V++ CSV/JSONL at sizes above 16 KiB are marked `skipped` by default because
the current splitter/parser path scales badly and can consume many minutes.
To explicitly measure those very large cases, pass
`--max-vpp-structured-kib 0 --timeout-s 1200`. This never assigns skipped
cells a zero duration.

The benchmark intentionally measures **in-process execution**, after a
correctness preflight. Per-process wall time is recorded independently. Fixtures
are created outside timing. Repeated reads typically hit the filesystem's page
cache; this is **warm-cache reading**, not a storage-device cold read benchmark.
Results from different hosts cannot be compared as though measured on one CPU.
Small inputs measure overhead and should not be extrapolated to large files.
Calibrated repetitions happen within one process and are divided by their count.
Feature rates represent **whole workload batches per second**, not individual
container operations or function calls. A single-call pilot determines the
repeat count; JIT runtimes can still be influenced by compilation/warmup during
the timed batch. The first-party V++ VM is forced to the same non-JIT execution
mode for each invocation. Check `median_process_wall_ms` separately for startup.

## Data operations

| Operation | V++ VM | Python | C++ | Node | Other Python/native variants |
| --- | --- | --- | --- | --- | --- |
| Materialize binary file (`read_bytes`) | Yes | `read()` | `ifstream` | `readFileSync` | `readinto(64 KiB)` (reusable buffer), `mmap` (page-touch only) |
| Write binary file (`write_bytes`) | Yes | `write()` + close | `ofstream` + close | `writeFileSync` | — |
| Sum all binary bytes (`bytes_sum`) | Yes | `sum(bytes)` | `uint64` loop | JS loop | NumPy `fromfile().sum()` |
| Read/decode UTF-8 (`read_text`) | Yes | `read(encoding='utf-8')` | strict UTF-8 validation/count | UTF-8 decode/count | — |
| Stream in 64 KiB chunks (`stream_64k`) | Yes | binary `read(65536)` | `ifstream.read` | `fs.readSync` | — |
| 32 random one-byte reads (`seek_sample`) | `Bộ Đệm Tệp.dịch` + read | `seek` + `read(1)` | `seekg` + `get` | `fs.readSync` at offset | — |
| Parse simple unquoted CSV (`csv_split`) | Yes | `split()` | `find()` + parse | `split()` | `csv.reader`, pandas, Polars |
| Parse JSON Lines (`jsonl_parse`) | Yes | `json.loads` | — | `JSON.parse` | — |

All variants must return the same independently computed numeric result.
For `write_bytes`, the input is materialized **before** the timed section
and the runner checks the **SHA-256 of the entire output file** after every
child process exits. The timed section includes writing and close/flush but
not `fsync`/storage durability. Writes go to a dedicated output directory,
never overwrite the binary input fixture.
Structured parsing measures loading plus parsing plus summing two numeric
columns. `csv_split` is explicitly limited to fixture rows with **no quoting**;
it is not a full CSV parser. C++ JSONL is omitted because this suite does not
bundle a comparable general-purpose C++ JSON parser. Absent runtime/library
support is **missing coverage**, never recorded as zero execution time.

Python libraries such as NumPy, pandas and Polars are optional and only run if
already installed. These use optimized native kernels internally. `mmap`
touches one byte per page and does not allocate a full copy: its reported rate
describes a different operation from V++ `đọc các byte tệp`. V++ returns a VM
**list of integer bytes**, while Python `read()` returns a compact `bytes`
object, so differences include data representation and conversion overhead.
The manual GitHub Actions `standard` profile installs NumPy, pandas and Polars;
`smoke` works with only the system Python standard library. Package versions
are captured in `results.json` so separate runs can be interpreted correctly.
`seek_sample` reports batches/s (32 one-byte reads per batch); it does not
claim that an entire file has been read. V++/C++/Python use buffered file seek
and read; Node.js uses positional `readSync` (typically implemented as `pread`).
These measurements compare end-user library APIs, not identical syscalls.

## Language feature coverage (as of this benchmark)

| Family | Measured workloads | Gaps to implement |
| --- | --- | --- |
| Arithmetic/branching | `arithmetic`, `branch_loop`, `fibonacci` | floating point, overflow boundaries |
| List/collections | `list_128`, `list_append`, `map_lookup`, `set_dedup`, `sort_numbers`, `linear_search`, `filter_sum` | nested collections, alternative sorting/search algorithms |
| Text/Unicode | `unicode_replace`, `read_text`, `string_concat` | normalization, regex |
| JSON | `json_parse`, `jsonl_parse` | serialization, large nested documents |
| I/O | `read_bytes`, `write_bytes`, `stream_64k`, `read_text`, `seek_sample` | concurrent I/O, true cold-cache reads |
| Bulk data | `bytes_sum`, `csv_split`, `jsonl_parse`, `filter_sum` | columnar Arrow/Parquet, grouped aggregation |
| Functions/OOP | `function_calls`, `recursion` | OOP virtual/dynamic dispatch |
| Memory/GC | `alloc_objects` | collection pressure, peak RSS |
| Concurrency | — | threads, workers, async |
| Networking | — | TCP, DNS, HTTP/TLS, loopback server |
| Runtime/compiler | independent benchmarks under `scripts/quality/` | compile/load/startup phase report in this suite |
| Crypto/error handling | `exceptions` | hashing, TLS |

For `sort_numbers`, `linear_search`, and `set_dedup`, all runtimes
produce the same result with matching input. The **public collection APIs**
are compared: V++ currently uses quadratic bubble sort and a list-backed
set implementation, while Python/C++/Node use optimized native sorting and
hash-set structures. Thus these results compare **application-level API
performance**, not identical algorithms. The sort workload is capped at
64 elements and set workload at 128 to avoid quadratic tests dominating
the manual workflow. `feature_n` in results records the actual size.
`filter_sum` creates an N-element integer list and aggregates values divisible
by three; optional NumPy uses an optimized vectorized implementation. Small N
values mostly measure API/setup overhead. Ratios in `comparisons.md` are
application-level timing ratios; they do not isolate VM dispatch, JIT speed,
allocation policy, or an individual library function.

The suite covers a representative set of portable APIs, **not every V++ API**.
Open gaps are reported explicitly and must not be presented as benchmarked.
This extension currently provides **19 workloads**: 8 file/data operations
and 11 language-feature operations. The independent portable application suite
covers additional algorithmic cases and languages (C, Java, Rust, Ruby).

The existing portable application benchmarks and this bulk-data suite are
complementary. Use matched algorithms to isolate language/runtime overhead;
use best-available APIs (NumPy/pandas/Polars) to compare real application
throughput. Never interpret either category as interchangeable.

The new bulk benchmark is **manual**: it is not added to CTest or the PR CI
gate. `.github/workflows/benchmark-bulk-io.yml` exposes a separate dispatch
workflow with 4 KiB smoke and an explicitly selected longer profile.
