# Benchmark baseline

Baseline recorded on 12/09/2026 with a Release build on macOS 26.6.2 arm64,
Apple Clang 21.0.0. These numbers are reference observations only; compare runs
on the same machine and build mode before treating a change as a regression.

| Benchmark | Iterations | Total | Per iteration |
| --- | ---: | ---: | ---: |
| VM dispatch | 250 | 3.396 ms | 13,585.7 ns |
| Lexer | 5,000 | 29.519 ms | 5,903.8 ns |
| Compiler pipeline | 250 | 9.999 ms | 39,995.0 ns |
| Native HTTP helpers | 100,000 | 297.695 ms | 2,976.9 ns |

The benchmark executable prints the same machine-readable fields on every run:
`benchmark`, `iterations`, `total_ms`, and `ns_per_iteration`.
