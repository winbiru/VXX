# Native i32 compiler: executable linkers and bootstrap

This covers `gói/biên dịch/bản địa/bộ biên dịch.vi`, the native i32 compiler.
It does not establish complete language parity with the production VM compiler.
Compiler object generation, symbol resolution and executable serialization in
these paths are implemented in V++. No existing worktree changes are discarded.

Verified on 2026-11-02: the Linux full source bootstrap, macOS native executable
bootstrap and Windows PE bootstrap through Wine all pass. The macOS gate
rejects 14 corrupted boundary inputs; the PE gate now rejects 15. The existing
freestanding Mach-O and PE executable gates also pass. Complete compiler hashes
and the distinction between Wine and native Windows are recorded in
[compiler-self-hosting-status.md](compiler-self-hosting-status.md).

Reverified on 2026-11-03: Linux's source-only gate passed on Ubuntu AMD64;
macOS ARM64 Stage 1/2 matched SHA-256
`32fb0e2a6869baade1760e51ce053088e189120255154cfc6ea019c3d7080bc9`;
Windows PE Stage 1/2/3 matched SHA-256
`870cbbbf8c6138259fef31aacbca2754cf710444bb45d8954c0ac437f3c1404e`
under Wine in Docker. Windows runtime probes and malformed COFF rejection were
observed with the self-hosted PE compiler. Real Windows execution remains
unverified.

## Linux x86-64

Packed archives use repeated `[u32 object length][ELF64 ET_REL bytes]` records;
they are not Unix `ar` archives. The bounded byte arena accepts up to 2 MiB.
The ELF file reader now reads until EOF instead of stopping after 257 blocks.
At exactly 2 MiB it probes EOF without attempting to append to the full arena;
an additional byte or a read error rejects the input.

`check_native_elf_partial_linker.py` checks an archive larger than 1 MiB,
executes its linked code with exit 42, accepts exactly 2 MiB, rejects a larger
file at the reader, and rejects a truncated archive near the limit. Rejected
links preserve the previous destination and leave no temporary output.

The full compiler entrypoint can be selected for the ELF bootstrap gate:

```sh
VPP_LINUX_COMPILER_SOURCE="$PWD/gói/biên dịch/bản địa/bộ biên dịch.vi" \
  bash scripts/quality/check_native_elf_linker.sh /path/to/seed /new/output
```

Use a host object assembled from the current source. In particular, the former
host object with a `0x4101400` arena cannot serve all 64 handles after the
runtime's payload stride was enlarged. The current host reserves `0x8101400`.
Do not reuse an old object solely because it still links small programs.

`check_linux_source_bootstrap.sh` performs the stronger check: starting with
text sources and one trusted seed, it assembles the host, rebuilds the compiler,
compares successive compiler and host generations, runs the archive and
assembler regressions, and forbids external build tools. The seed must support
the current `assemble` command, 2 MiB isolated byte buffers and sufficient
ELF object/archive emission capacity for the selected compiler entrypoint.
The gate first exercises a 2 MiB buffer while keeping an adjacent buffer live,
checks data integrity at the start, the 1 MiB boundary and the last byte,
verifies that one extra byte fails without altering existing bytes, and then
exercises the real compiler archive. The byte-buffer probe alone cannot
establish ELF emitter capacity.

On 2026-11-03, the older local `vpp-source-seed-v2` (704,544 bytes) passed
small ELF smoke and the byte-buffer probe but exited with code 16 when emitting
the current compiler. The previously self-hosted Linux stage 2 seed from
2026-11-02 (761,888 bytes) successfully rebuilt the current full entrypoint:
Linux stage 1 and stage 2 matched byte-for-byte at SHA-256
`b1e3a8caa238952f37b17dff963860cecb1d0688b12972ae999a4435451d1ebc`.
If the old seed reports `SOURCE_BOOTSTRAP_SEED_ELF_ARCHIVE=FAIL`, supply a
newer verified ELF-capable seed. Do not infer the root cause of every exit 16
without inspecting the emitting compiler.

For a full compiler source bootstrap in a Linux x86-64 shell:

```sh
bootstrap_out=/absolute/new/linux-bootstrap
VPP_LINUX_COMPILER_SOURCE="$bootstrap_out/source/gói/biên dịch/bản địa/bộ biên dịch.vi" \
  bash scripts/quality/check_linux_source_bootstrap.sh /path/to/current-seed "$bootstrap_out"
```

Reverified on 2026-11-03 using Ubuntu 24.04 AMD64 in Docker, with read-only
access to the repository and a separate writable output mount at
`/private/tmp/vpp-selfhosting-verify-20261103/linux-bootstrap-verified-final`.
The complete gate finished with exit code 0, including
`SOURCE_BOOTSTRAP_SEED_ARENA_2M=PASS`,
`NATIVE_ELF_REL_ARCHIVE_EXACT_2MIB=PASS`,
`NATIVE_ELF_REL_REGRESSIONS=PASS`,
`SOURCE_BOOTSTRAP_PLATFORM_DIR_ONLY=PASS`,
`NATIVE_ASSEMBLER_REGRESSIONS=PASS`,
`SOURCE_BOOTSTRAP_CONVERGENCE=PASS` and
`SOURCE_BOOTSTRAP_EXTERNAL_BUILD_TOOLS=NONE`.
The stage 1 and stage 2 compilers matched SHA-256
`b1e3a8caa238952f37b17dff963860cecb1d0688b12972ae999a4435451d1ebc`.
All three host objects matched SHA-256
`3cbdfa901b4309f6347175c70bf2c47b5d540b9899b0a94f3136d849eb45fd06`.
The stronger boundary-content test also passed in an independent Docker run.

## macOS ARM64

Enable the executable linker with both environment variables:

```sh
VPP_NATIVE_MACHO_EXE_LINK=1 \
VPP_MACOS_RUNTIME_OBJECT=/absolute/path/to/runtime.o \
VPP_HOME="$PWD" /path/to/seed build \
  'gói/biên dịch/bản địa/bộ biên dịch.vi' -o /new/compiler
```

The runtime object is assembled once from
`scripts/ASM/macos/vpp_aot_primitives_aarch64.S`. The V++ linker combines the
implicit V++ runtime units, source modules and this platform boundary. It emits
MH_EXECUTE with separate RX text and RW data, dyld binding records, import
stubs and LC_MAIN. It handles the boundary's ARM64 BRANCH26, PAGE21 and
PAGEOFF12 instruction relocations. Unsupported relocations, shifted ADD
PAGEOFF12, duplicate definitions and imports outside the libSystem catalog
are rejected before publication.

Apple `ld` and the SDK are unnecessary during these compiler generations.
The running executables still use dyld/libSystem, and `/usr/bin/codesign`
adds the required ad hoc signature. A fixed signing identifier makes the
entire signed executable deterministic. With no runtime object, the existing
freestanding native executable path remains available.

```sh
bash scripts/quality/check_macos_native_executable_selfhosting.sh \
  /path/to/current-native-seed /new/macos-output
```

This gate prepares ASM once, blocks external compilers/assemblers/linkers,
compares two compiler generations byte for byte, verifies code signatures and
loader mappings, runs Unicode and machine-runtime probes, and checks malformed
runtime objects without overwriting the prior destination.

## Windows AMD64

The PE executable linker can include the V++ runtime and an ASM platform
object when `VPP_WINDOWS_RUNTIME_OBJECT` is set. Cross-generation uses
`VPP_NATIVE_TARGET=windows-x86_64`; a PE compiler also detects its Windows host.
The source-object and runtime-object CLI modes retain their separate exports.

The platform input accepts the boundary's ordinary `.text`, `.rdata`, `.data`
and empty `.bss` COFF sections. Supported machine references are REL32 through
REL32_5. The linker emits deterministic PE32+ with RX code, RW data, import
descriptors, lookup tables, IAT entries and RIP-relative import thunks.
Imports are restricted to explicit functions in kernel32, msvcrt, shell32 and
bcrypt. Unresolved symbols and unsupported sections/relocations fail closed.
COFF section names and characteristics are validated as pairs; a forged
`.text` section bearing `.data` write flags (or a renamed `.data` bearing
`.text` execute flags) is rejected before the executable is published.

This is a fixed-base image with relocations stripped and NX enabled. It does
not support arbitrary COFF libraries, absolute relocations, TLS, COMDAT or
unwind metadata. The current compiler/runtime requires none of those forms.
The PE entry calls V++ main directly; the platform boundary uses Windows and
CRT DLL APIs without a MinGW or MSVC executable-link step.

```sh
bash scripts/quality/check_windows_native_pe_selfhosting_docker.sh \
  /path/to/current-native-seed /new/windows-output
```

Clang assembles the boundary once on the host. V++ cross-creates stage 1;
stage 1 creates stage 2 and stage 2 creates stage 3 as PE processes under
Wine in an Ubuntu AMD64 Docker container. The gate compares all three complete
executables, executes Unicode and runtime probes, inspects PE mappings/imports,
tests invalid boundary objects and blocks external build tools during stages.
`LANG=C.UTF-8` and `LC_ALL=C.UTF-8` are required for Wine's Unix path mapping.

Wine evidence is not a native Windows runner result. The equivalent direct
Windows gate is provided separately (PowerShell 7):

```powershell
pwsh -File scripts/quality/check_windows_native_pe_selfhosting.ps1 `
  -SeedCompiler C:/bootstrap/stage1.exe `
  -RuntimeObject C:/bootstrap/windows-runtime.obj `
  -OutputDirectory C:/new-output
```

That gate has not been executed on a real Windows host in this local macOS
session. It requires a PE seed and the preassembled boundary, then generates
three compilers directly on Windows and compares SHA-256 hashes.

## Format references

The serializers and independent inspectors use the
[Apple Mach-O declarations](https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/loader.h)
and [Microsoft PE/COFF format](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format).
