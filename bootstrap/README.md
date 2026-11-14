# V++ platform bootstrap

## macOS ARM64

The two seed executables are generated V++/ASM Mach-O binaries, not the old C++ host. They are pinned by `macos-arm64/SHA256SUMS`. The full-language seed is the 2026-11-13 autonomous stage3 from `full-native-selfhost-v5`; the i32 seed is the 2026-11-13 refreshed benchmark runtime compiler. Both link only `/usr/lib/libSystem.B.dylib` and have an ad-hoc signature. These are bootstrap inputs, not the final compiler: the build regenerates the i32 compiler, runtime object and full compiler from the current `.vi` sources. The previous-generation full seed emits Assembly, so the first full compiler build may use the system assembler/linker as well as the two ASM platform objects. The generated current compiler uses the direct V++ ARM64/Mach-O route for user programs. No C or C++ source is compiled.

The bootstrap runtime is kept separate from the published runtime: Apple `ld -r` adds symbol flags that the direct Mach-O linker rejects. The build regenerates the final runtime object with `VPP_NATIVE_MACHO_REL_LINK=1` using the freshly built i32 compiler.

To use independently verified replacement seeds, set `VPP_BOOTSTRAP_FULL` and `VPP_BOOTSTRAP_I32` at CMake configure time. The build checks the target, signature and linked libraries of both seeds; the checksum manifest pins the bundled seeds only.

## Linux x86-64 i32 seed

`linux-x86_64/vpp-i32` is the static ELF stage 2 from the 2026-11-07 `weak-elf-check` run (`NATIVE_ELF_SELFHOST_CONVERGENCE=PASS`). Its SHA-256 is pinned alongside the executable. The Linux CI job audits the seed and rebuilds the ASM host and compiler from text in an isolated directory with external build tools blocked.

## Linux x86-64 full compiler seed

`linux-x86_64/vpp-full.S.gz` contains standalone preprocessed x86-64 Linux compiler assembly. The initial seed comes from the 2026-11-13 full V++ compiler cross-emission (`full-linux-port/linux-full-compiler.S`). Its original assembly SHA-256 is `1ce1a20ce0907fd3e86d7c1241d9f7124d4adde6f09e8711e30426bb1faea8db`; the preprocessed assembly SHA-256 is `5abff8f7a696ef28b8421a3e5ad1d79e1d89fc07483dfd92405d4b5fb19e4273`. The compressed seed is pinned in `linux-x86_64/SHA256SUMS`.

The original and preprocessed assembly produced byte-identical ELF relocatable objects with SHA-256 `2cc6a8a9353c9e647db6dd702b004df24d61b6f2fd4a9f0eeccba3d2d7b43e62` in the initial cross-assembly check. The compiler, nine Linux ASM boundary objects and V++ runtime object also passed static symbol compatibility checks. These checks do not establish native Linux execution. The full Linux link, execution, convergence and full regression results remain to be verified on Linux x86-64.

On Linux x86-64 with Bash, clang, the system ELF linker, binutils, Python 3 and GNU coreutils, run from the repository:

```sh
bash scripts/quality/check_linux_full_bootstrap.sh /tmp/vpp-linux-full-bootstrap
```

The output directory must not exist. Optionally pass a Linux i32 prefix (as produced by the same checkout's i32 CI job) as the second argument. The script checks both seed hashes, builds i32 from source if no prefix is provided, regenerates the runtime from the current V++ sources, links stage1 from the pinned assembly and builds stages2/3 from the current full compiler source. Each generation builds and runs dynamic V++ fixtures; stages2 and 3 must be byte-identical. `LINUX_FULL_BOOTSTRAP_CONVERGENCE=PASS` is printed only after these gates pass. The CI then packages stage3 and runs all 16 full regression shards. There is no dependency on a macOS CI job or its artifacts. The full compiler still uses clang and the system linker for assembly and linking; no C/C++ implementation source is compiled.

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
The Windows i32 gate does not establish full dynamic compiler parity; Windows full bootstrap remains unverified.
