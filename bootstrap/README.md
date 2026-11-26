# V++ platform bootstrap

## Current full assembly seeds (2026-11-26)

All three `vpp-full.S.gz` seeds were refreshed from source commit `b046ae3` using the macOS ARM64 full compiler (executable SHA-256 `cb4bd88e9f9da3581cc1f873f7fd387c8d8046849da198a101660bc47b982271`), with compiler workers disabled. The preceding assembly seeds predated random-write, file sync and directory sync and could not resolve their intrinsic calls in the current library at Stage 1. The i32 executable pins and macOS full executable pin are unchanged. The older assembly refreshes described below are historical provenance.

Each target was emitted using `emit-program-asm`, preprocessed with `-DVPP_AOT_VPP_RUNTIME=1 -I scripts/ASM -I scripts/ASM/common`, and assembled both before and after preprocessing with Apple clang. The resulting objects matched byte-for-byte. The standalone assembly was compressed using `gzip -n -9`; all checksum manifests and the source-derived intrinsic coverage test pass.

| Target | Raw assembly SHA-256 | Standalone assembly SHA-256 | Object SHA-256 |
| --- | --- | --- | --- |
| `x86_64-unknown-linux-gnu` | `7a2258d7a5c75cc730428ae2a30da0699093e93d3d372a74c02b926e8bd748f1` | `cc543e75e03c165c6da134c086a12a47a6031adda350c17c3937d2a524070fcd` | `ec4f26a674b69da771df66b65fad052b4642ab44ba6d1a7395a52e090800a478` |
| `aarch64-apple-darwin` | `4f52a7db35f9029780d474899f426c6985471b70acf71c267830c7dbbe626a48` | `1b23f1cc4bcfc33fb46806aae335af4c5b154837bdb2824f5019f122d6025113` | `7cca69ded0e085cbadd5e149e1200744ecc54394355d360761bba3dfd69c3634` |
| `x86_64-pc-windows-msvc` | `f5e5f470ee62ac4b78fdbdc9c4315bf496d9b98c5618626871d5a5685ec195f8` | `174ad4cd417930651a39011bed296c1f67272eb76704f8b7c5a911209bd9fd4a` | `557814641d529c59d4927610b4817de199fa46e05f7bf05f3776def88aaba43e` |

The relinked macOS Stage 1 passed its code signature and dependency checks, compiled `native_path_kind.vi` with the external assembler disabled, and ran the fixture successfully. On Wine, the relinked Windows Stage 1 passed the PE audit and `--version`, and emitted assembly for a probe calling all three new intrinsics. The previous seed failed the same probe with an unresolved `máy tệp ghi tại` call. The initial Wine run used the POSIX locale and failed on Unicode paths. With `LANG=C.UTF-8` and `LC_ALL=C.UTF-8`, the refreshed Windows Stage 1 resolved the Unicode imports and emitted `native_path_kind.vi` successfully; the linked executable ran under Wine and printed `NATIVE PATH KIND OK`. On Linux, directly compiling the current runtime with the older pinned i32 still failed in `vm_slice` (exit 14), even with a current ASM host object. The normal source bootstrap rebuilt the ASM host from text, produced byte-identical i32 Stage 1 and Stage 2 compilers, and completed the current runtime object build. The refreshed full seed then linked, passed the static ELF audit, and compiled and ran all eleven Linux Stage 1 dynamic fixtures successfully under emulated Linux amd64. The full Windows fixture suite and native CI, including full Stage 2/3 convergence, remain to be verified.

## macOS ARM64

The two seed executables are generated V++/ASM Mach-O binaries, not the old C++ host. They are pinned by `macos-arm64/SHA256SUMS`. The full seed is the locally verified 2026-11-14 stage3 from `vpp-macos-ci-split`, SHA-256 `832554cf16894f06a2a85e0aa21796e811334c1984ac98be424fe391e1cf8cd0`. Its stage2 and stage3 were byte-identical, both passed the four dynamic AOT golden fixtures, and it compiled and ran a direct Mach-O probe with the assembler unavailable as well as a legacy assembler/linker probe. It predates the current batch-build revision; the exact source commit of this local seed was not recorded. The i32 seed remains the 2026-11-13 refreshed benchmark runtime compiler. Both link only `/usr/lib/libSystem.B.dylib` and have an ad-hoc signature. No C or C++ source is compiled.

The bundled `macos-arm64/vpp-full.S.gz` is standalone preprocessed ARM64 compiler assembly emitted on 2026-11-16 from source commit `9929796` by the local full compiler (executable SHA-256 `772dfcf8a5dd000d22a0bd9dc460a7a3f51cbfaf38cf0bbb6ff95695edee922e`). This refresh includes the Mach-O import catalog for the OS worker runtime (`_munmap` and the six `_pthread_*` imports). Raw assembly SHA-256 `cce113747fc706e72a9e0359fce6930e38b2aaea2afff57484c5f156e688b750`; preprocessed SHA-256 `712de017b39a03e8aa3ee0ec51530d8ed51a274e31f7c9a5c1c204a000f0cd8b`. The original and preprocessed assembly produced identical Mach-O objects (`691ed65030f87fe16e53d19e5a6d1b67dc00098e60c4d2a12b83a537c91454ed`). The relinked seed passed signature/dependency checks and built and ran the dynamic AOT fixture with the current-source runtime and the external assembler disabled. Full convergence must still be verified. The default seed is relinked on every bootstrap; an explicitly supplied independent executable is used as provided.

The bootstrap runtime is kept separate from the published runtime: Apple `ld -r` adds symbol flags that the direct Mach-O linker rejects. The build regenerates the final runtime object with `VPP_NATIVE_MACHO_REL_LINK=1` using the freshly built i32 compiler.

macOS native CI has three jobs: `macos-i32` rebuilds i32 and the bootstrap runtime from source and uploads them as a tar archive; `macos-full` consumes that archive via `VPP_PREBUILT_I32_RUNTIME` and configures `VPP_CI_PINNED_FULL_STAGE1=ON`. Stage 1 relinks the pinned compiler assembly against the freshly built runtime and platform objects, regenerates the canonical runtime, builds the shared VM and runs five fixtures. Stages 2 and 3 each build the full compiler from the current `.vi` sources using direct V++ ARM64/Mach-O with the external assembler disabled. Both stages must execute dynamic AOT fixtures, pass code signature and dependency checks, and match byte-for-byte (including their SHA-256 hashes). Stage 3 is the compiler published in the full package. `macos-regression` then runs batch parity and both suites from that package. This removes one redundant full-source build from CI, aligning the generation count with Linux and Windows. Ordinary CMake builds default to `VPP_CI_PINNED_FULL_STAGE1=OFF` and still compile the current full source before returning an executable.

To reproduce the full convergence gate after a normal CMake build:

```sh
bash scripts/quality/check_macos_full_bootstrap.sh build-pure-vpp /tmp/vpp-macos-full-bootstrap
```

Use a fresh output directory. The initial CMake compiler is stage 1 for this gate; in an ordinary build the pinned seed is an input to that build. In CI mode the pinned assembly relinked with the current runtime is stage 1.

To use independently verified replacement seeds, set `VPP_BOOTSTRAP_FULL` and `VPP_BOOTSTRAP_I32` at CMake configure time. The build checks the target, signature and linked libraries of both seeds; the checksum manifest pins the bundled seeds only.

## Linux x86-64 i32 seed

`linux-x86_64/vpp-i32` is the static ELF stage 2 from the 2026-11-07 `weak-elf-check` run (`NATIVE_ELF_SELFHOST_CONVERGENCE=PASS`). Its SHA-256 is pinned alongside the executable. The Linux CI job audits the seed and rebuilds the ASM host and compiler from text in an isolated directory with external build tools blocked.

## Linux x86-64 full compiler seed

`linux-x86_64/vpp-full.S.gz` contains standalone preprocessed x86-64 Linux compiler assembly. The 2026-11-16 refresh was cross-emitted from source commit `9929796` by the rebuilt macOS ARM64 full V++ compiler (executable SHA-256 `eb01fba5507908da64f8987f22b113943206807fabc7036aa08438ae153b0e44`) with compiler workers disabled. It includes the current worker runtime, lowering and source diagnostics changes. The original assembly SHA-256 is `d3bf39bd615cf3681beb4a303cd8d650841b82b005d5b4498d3e3bef00905530`; the preprocessed assembly SHA-256 is `a1b5fb70145ae717f03e87063fdbf42182f2432251215be457dca0991817a045`. The compressed seed is pinned in `linux-x86_64/SHA256SUMS`.

The refreshed original and preprocessed assembly produced byte-identical ELF relocatable objects with SHA-256 `33c0950f3d1acd042d9eaf2c780f5052261e219bc603d12ae182eaa38413a86d`. On 2026-11-16 an emulated Linux amd64 container rebuilt i32 and its ASM host with exact convergence, rebuilt the V++ runtime, linked the refreshed full seed, passed the static ELF audit and compiled and ran all eleven stage1 dynamic fixtures. The relinked stage1 executable SHA-256 was `20cab5e7eadcd0c72b6100555ceea965961cf466a5e84468d7c3f4b4ad5f7e3d`. The full bootstrap still requires a strict stage2/stage3 byte comparison; on failure it prints both hashes and ELF layouts.

On Linux x86-64 with Bash, clang, the system ELF linker, binutils, Python 3 and GNU coreutils, run from the repository:

```sh
bash scripts/quality/check_linux_full_bootstrap.sh /tmp/vpp-linux-full-bootstrap
```

The output directory must not exist. Optionally pass a Linux i32 prefix (as produced by the same checkout's i32 CI job) as the second argument. The script checks both seed hashes, builds i32 from source if no prefix is provided, regenerates the runtime from the current V++ sources, links stage1 from the pinned assembly and builds stages2/3 from the current full compiler source. Each generation builds and runs dynamic V++ fixtures; stages2 and 3 must be byte-identical. `LINUX_FULL_BOOTSTRAP_CONVERGENCE=PASS` is printed only after these gates pass. The CI then packages stage3 once and runs one full Linux JIT regression job with two workers. The i32 suite and packaged platform object parity are checked separately. HTTP clients are serialized against one shared server. Compiler, linker and VM harnesses retain compilation as part of their assertions. The i32 suite uses the same packaged self-hosted i32 compiler. There is no dependency on a macOS CI job or its artifacts. The full compiler still uses clang and the system linker for assembly and linking; no C/C++ implementation source is compiled.

After native convergence and regression have been verified, a replacement seed can be emitted by the verified Linux full compiler. Use its installation root and runtime, emit to a fresh directory, preprocess and compare the assembled objects before pinning:

```sh
export VPP_HOME="$PWD" VPP_AOT_VPP_OBJECT=/absolute/path/to/verified/runtime.o
/absolute/path/to/verified/vpp-native-stage3 emit-program-asm --target x86_64-unknown-linux-gnu \
  'gói/biên dịch/dòng lệnh AOT tự chủ.vi' -o /tmp/vpp-linux-compiler.S
clang -target x86_64-unknown-linux-gnu -DVPP_AOT_VPP_RUNTIME=1 \
  -I scripts/ASM -I scripts/ASM/common -E -P -x assembler-with-cpp \
  /tmp/vpp-linux-compiler.S -o /tmp/vpp-linux-full.S
clang -target x86_64-unknown-linux-gnu -DVPP_AOT_VPP_RUNTIME=1 \
  -I scripts/ASM -I scripts/ASM/common -c /tmp/vpp-linux-compiler.S -o /tmp/vpp-linux-original.o
clang -target x86_64-unknown-linux-gnu -c /tmp/vpp-linux-full.S -o /tmp/vpp-linux-standalone.o
cmp /tmp/vpp-linux-original.o /tmp/vpp-linux-standalone.o
gzip -n -9 -c /tmp/vpp-linux-full.S > bootstrap/linux-x86_64/vpp-full.S.gz
(cd bootstrap/linux-x86_64 && sha256sum vpp-i32 vpp-full.S.gz > SHA256SUMS)
```

Record the generating compiler's provenance and the raw/preprocessed assembly and object hashes; rerun the full bootstrap and regression against the replacement seed before treating it as verified.

## Windows x86-64 i32 seed

`windows-x86_64/vpp-i32.exe` is stage 3 of the 2026-11-13 `windows-pe-fix-selfhost` Wine run. All three saved PE generations have identical SHA-256, and the independent PE layout audit reports 39 imports. The native Windows CI gate rebuilds the boundary from ASM and regenerates all three compiler stages from V++ source. No macOS build artifact is needed by this job.

## Windows x86-64 full compiler seed

`windows-x86_64/vpp-full.S.gz` contains standalone preprocessed x86-64 Windows compiler assembly. The 2026-11-22 refresh was cross-emitted from the `Feature/TLS` working tree at `5f46a8e` (with local source changes) by the macOS ARM64 full V++ compiler (executable SHA-256 `5070b5d86ba28f7ed296bb390b4d51a33ad634b0475f0154a666675a6fe40361`), with compiler workers disabled, targeting `x86_64-pc-windows-msvc`. The AOT CLI source SHA-256 was `2c46d6e331a7688cc1da4f1d9da88df64eb13476b1450c556ef8a30ecd245e00`. This refresh carries `-lsecur32` and `-lcrypt32` into the Windows Stage 1 executable linker for Schannel/SSPI and CryptoAPI. The raw assembly SHA-256 is `fe41cfa6db60585494cbaac23229e4e73f81e3e2adcb78fd4b86898894dc7386`; the preprocessed assembly SHA-256 is `35cc89b9a6ac8782a02b5882443ecf78bd3e37a06fdffb1c5c1ea14993f96cf3`. The compressed seed is pinned in `windows-x86_64/SHA256SUMS`.

The refreshed raw and preprocessed assembly generated byte-identical AMD64 COFF objects with SHA-256 `e34b7d576951a4b8d8cca4bdb7e92e51b6bc62cf98241a3153173ff7ec42c2fa3` using Apple clang targeting Windows. The bootstrap checks the pinned seed for both Schannel link arguments before proceeding. A Wine compatibility run linked Stage 1 with the V++-generated Windows runtime and Win32 ASM boundary using MinGW, passed the PE layout audit, ran `--version`, then emitted, linked and ran the `hello.vi` fixture with output `42`. Native Windows CI with clang/lld, Stage 2/3 convergence and Windows Schannel TLS runtime checks remain to be verified. If stage2 and stage3 differ, the convergence gate saves both SHA-256 hashes, first differing file offset, and PE section layouts and hashes before failing.

On Windows x86-64, install Python 3, PowerShell, CMake and the MSYS2 CLANG64 clang/lld/LLVM toolchain. From PowerShell at the repository root, with that toolchain in `PATH`, run:

```powershell
python scripts/quality/check_windows_full_bootstrap.py --clang (Get-Command clang.exe).Source bootstrap "$env:TEMP/vpp-windows-full" --archive "$env:TEMP/vpp-windows-full.zip"
```

Use fresh output and archive paths. Optionally supply `--i32-prefix` pointing to the i32 package produced by the same checkout's Windows CI. The gate verifies both seed hashes, regenerates the implicit/core V++ runtime and Win32 ASM boundaries, links stage1 and builds stages2/3 from source. Each stage builds and executes twelve fixtures (36 executions), including the native worker fixture; stages2/3 must be byte-identical. The package includes the full compiler, i32 compiler, standalone VM, DNS provider, launcher and full/raw runtime objects. It is extracted and tested with native execution, VM bytecode, DNS hosts resolution and launcher smoke checks. The CI then extracts the package once and runs the full and i32 regression suites in one Windows job. Each suite builds its independent AOT fixtures once and executes the prepared binaries with two workers; HTTP clients share one server and are serialized. All seven standalone VM phases are enabled. Compiler, linker and VM harnesses retain compilation as part of their assertions. No macOS or Linux job artifact is required. Clang and lld assemble/link the V++/ASM output; no C/C++ implementation source is compiled.

The Windows process phase also runs a V++ fixture in both native AOT and standalone VM modes. An independent Python child checks Unicode and quoted argv, Unicode environment values, 64 KiB on each pipe, deadline handling after both pipes close, and descendant termination after a JobObject timeout.

The native Windows full link, execution, stage convergence, package checks and full regression still require a successful run on Windows x86-64. Once verified, refresh the seed using the native stage3 compiler with `emit-program-asm --target x86_64-pc-windows-msvc`, preprocess it with `-DVPP_AOT_VPP_RUNTIME=1 -I scripts/ASM -I scripts/ASM/common`, compare the two assembled COFF objects, then compress deterministically and update the manifest and provenance.
