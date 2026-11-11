# Native AOT: direct assembler and linker

The AOT boundary accepts V++-emitted assembly, writes a temporary `program.S`,
assembles it to a platform object, links the runtime and platform primitives,
then atomically moves the executable to the requested output path. The
driver is implemented in `gói/biên dịch/trình điều khiển AOT.vi`, with session
and cache transactions in `gói/biên dịch/phiên AOT.vi`.

The self-hosted compiler's `build`, `run`, batch, embedded-source and artifact
routes use that V++ driver. It selects the runtime and platform ABI, finds
archives, constructs argv for the assembler/linker (including the CMake parity
route), and executes commands through `gói/hệ thống/tiến trình.vi`. Tool launch
and exit errors are handled in V++; no shell command string is constructed.

`gói/biên dịch/trình điều khiển AOT.vi` owns the self-hosted driver's
temporary-directory operations, dependency enumeration, linker configuration
signature, object/executable/source-index cache keys, staging, file publication,
batch output directories and cleanup. The C++ host roots V++ plans across GC
callbacks and relays profiling records and child-process output. The runtime
exposes individual OS primitives for metadata, path identity, working/temporary
directories, executable permissions, child processes and file-token locks.
V++ decides when to acquire/release those locks, rechecks cache eligibility,
selects cache hits, builds misses and publishes source indices. V++ allocates unique
staging directories for object, executable and source-index cache publication.
Immutable objects use a no-replace hard link, with a copy to the target volume
first for cross-filesystem sources. When hard links are unsupported, caching is
skipped to avoid overwriting another writer's entry. Updateable source indices
use atomic rename under their OS lock. The reusable CMake linker
directory is created and removed through V++ and lasts exactly one compiler
session, so successive batch compilations can reuse the linker safely.

The V++ session selects the toolchain/package installation roots, normalizes
CLI source/output paths, rejects output aliases of the source (including hard
links), chooses the library build root (`VPP_AOT_LIB_ROOT`),
decides when compiler profiling disables the source-index shortcut, and
writes/reads embedded UTF-8 sources and captured diagnostics through the V++
file API. It also resolves embedded compiler/base paths and prepares batch
source paths, working directories, memo configuration and fail-fast policy.
The bootstrap keeps a C++ fallback for callers that have not loaded
the V++ compiler functions. Host primitives still supply current/executable
paths, process identifiers, exclusive directory creation, a `PATH_STAT` syscall for
regular-file size and nanosecond mtime, executable
permissions and atomic rename/locking; these require OS-specific ABI calls.
Locks use opaque VM-owned file tokens; closing the token or destroying the VM
releases the kernel lock. Lock handles are not inherited by child programs.
V++ releases build locks before returning the executable for user-code launch.

`src/native/aot_driver.cpp` retains the original C++ AOT path for Stage 0
bootstrap/oracle and legacy runtime callers. Those legacy paths still have
native file operations; they are outside the self-hosted V++ driver migration.

| Host / runtime | Per-program pipeline | Availability |
| --- | --- | --- |
| macOS ARM64 / `cpp` (Stage 0) | Clang ASM + C bootstrap + static V++ archives -> Clang++ | Default |
| macOS ARM64 / `vpp` | Clang ASM + V++ runtime object + ARM64 ASM primitives -> Clang | Default |
| Linux x86-64 / `vpp-freestanding` | Clang ASM + V++ runtime object + Linux ASM primitives -> static ELF | Default |
| Linux x86-64 or ARM64 / `cpp` (Stage 0) | C/C++ ASM + C bootstrap + static V++ archives + static OpenSSL -> C++ linker | Default |
| Windows x64 / `cpp` (MSVC Stage 0) | Clang COFF ASM + MSVC C bootstrap + COFF primitives + V++ archives -> MSVC linker | Experimental opt-in |

For Linux and macOS Stage 0, set `VPP_AOT_DIRECT_LINK_CPP=0` to use the
existing CMake link project for parity testing. Instrumented Stage 0 archives
(coverage or sanitizers) also use that fallback, which derives the appropriate
link flags from the parent CMake build. On Windows, use
`VPP_AOT_WINDOWS_DIRECT_LINK=1` from a Visual Studio Developer Command Prompt with
`clang`, `cl` and `link` available to opt in; the default remains the CMake
link project until Windows ABI parity is validated.

For the V++ runtime paths, `VPP_AOT_RUNTIME=vpp` selects the macOS native
runtime, and `VPP_AOT_RUNTIME=vpp-freestanding` selects the Linux x86-64
freestanding runtime. `VPP_AOT_DIRECT_LINK=0` keeps the CMake oracle for these
two runtimes. `VPP_AOT_VPP_OBJECT` selects the already-built V++ runtime
object; it must be an absolute path. Set `VPP_NATIVE_PROFILE=1` to see the
`driver-aot-vpp`, `assemble-*-program`, `prepare-*-runtime` and
`link-*-executable` phase markers. `driver-aot-vpp` confirms that the V++ driver
planned the link; it is absent from the Stage 0 C++ oracle.

Useful validation commands from the repository root:

```sh
cmake --build build --target vpp-cli --parallel 4
ctest --test-dir build -R 'vpp-native-cli-compiler-bootstrap|vpp-native-vpp-aot-driver|vpp-native-real-assembler-smoke|vpp-native-batch-parity|vpp-native-linker-cache-parity' --output-on-failure
```

`vpp-native-vpp-aot-driver` runs the actual V++ planner for macOS ARM64,
Linux x86-64/ARM64 and Windows x64, including invalid runtime/target pairs and
subprocess launch errors. It also builds a full Stage 2 compiler through V++,
then uses Stage 2 to build/run a program and verifies output preservation after
a tool failure, Unicode/space-containing argv and temporary-file cleanup.
It separately exercises the V++ temporary-directory, library-root,
profiling-policy and embedded-file/diagnostic callbacks.
SHA-256/HMAC vectors cover empty input, block-padding boundaries, multiple
blocks, binary bytes and Unicode. The implementation uses V++ wrapping i32
bit operators, preserving existing SHA-256 cache keys while avoiding the cost
of per-bit loops during cache lookups. Path and file hardening tests cover
capability checks, canonicalization, hard-link identity, working-directory
errors, executable permissions, lock exclusion and release on VM teardown.
Planning checks do not establish executable parity on a foreign operating system.
The OS/file fixture also checks V++ metadata observation and linker cache-key
invalidation when a dependency is modified or appears. The metadata syscall
returns `["present", size_bytes, mtime_seconds, mtime_nanoseconds]` (each value
is a string) or `["missing"]`; other OS stat failures return an internal
`["error", code]` that the V++ path module converts into a catchable error. The
fixture checks immutable cache publication, invalid sources and long-path
OS errors. Executable cache keys use `vpp-aot-executable-v5`; linker signatures
use `vpp-aot-linker-dependencies-v4`. Both are derived entirely in V++ from
typed inputs. Legacy cache entries are separated by these new versions.

Linux Stage 0 route parity runs inside `vpp-native-linker-cache-parity`. Keep
both direct and CMake link checks in CI before disabling the fallback. A local
macOS build can assemble the Linux x86-64 and ARM64 primitive objects using
Clang's cross-target assembler, but executing their ELF output requires a
Linux runner of the matching architecture.

This direct AOT path does not replace the Stage 0 C++ compiler used to bootstrap
the self-hosted V++ compiler. It removes the per-program CMake configure/build
step for supported native execution paths.
