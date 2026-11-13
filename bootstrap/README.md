# V++ bootstrap (macOS ARM64)

The two seed executables are generated V++/ASM Mach-O binaries, not the old C++ host. They are pinned by `macos-arm64/SHA256SUMS`. The full-language seed is the 2026-11-13 autonomous stage3 from `full-native-selfhost-v5`; the i32 seed is the 2026-11-13 refreshed benchmark runtime compiler. Both link only `/usr/lib/libSystem.B.dylib` and have an ad-hoc signature. These are bootstrap inputs, not the final compiler: the build regenerates the i32 compiler, runtime object and full compiler from the current `.vi` sources. The previous-generation full seed emits Assembly, so the first full compiler build may use the system assembler/linker as well as the two ASM platform objects. The generated current compiler uses the direct V++ ARM64/Mach-O route for user programs. No C or C++ source is compiled.

The bootstrap runtime is kept separate from the published runtime: Apple `ld -r` adds symbol flags that the direct Mach-O linker rejects. The build regenerates the final runtime object with `VPP_NATIVE_MACHO_REL_LINK=1` using the freshly built i32 compiler.

To use independently verified replacement seeds, set `VPP_BOOTSTRAP_FULL` and `VPP_BOOTSTRAP_I32` at CMake configure time. The build checks the target, signature and linked libraries of both seeds; the checksum manifest pins the bundled seeds only. The full-language compiler is currently supported on macOS ARM64; other targets have experimental V++/ASM components but no supported full bootstrap here.

## Linux x86-64 i32 seed

`linux-x86_64/vpp-i32` is the static ELF stage 2 from the 2026-11-07 `weak-elf-check` run (`NATIVE_ELF_SELFHOST_CONVERGENCE=PASS`). Its SHA-256 is pinned alongside the executable. The Linux CI job audits the seed and rebuilds the ASM host and compiler from text in an isolated directory with external build tools blocked. The Windows i32 job cross-creates its PE seed from the current V++ source on macOS, then runs the native PowerShell selfhosting gate on Windows. These two jobs verify the i32 subset; neither establishes full dynamic compiler parity with macOS. No Linux or Windows full-language release is produced by these gates.
