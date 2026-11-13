#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${1:-$ROOT_DIR/build/native-vpp-runtime}"
SEED_COMPILER="${2:-$ROOT_DIR/bootstrap/macos-arm64/vpp-i32}"
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) ;;
  Linux-x86_64)
    if [[ $# -lt 2 ]]; then
      echo 'Linux requires an explicitly supplied native V++ i32 seed.' >&2
      exit 2
    fi
    ;;
  *)
    echo 'The dynamic V++ AOT runtime currently supports macOS ARM64 or Linux x86-64.' >&2
    exit 2
    ;;
esac
if [[ ! -x "$SEED_COMPILER" ]]; then
  echo "Missing native i32 seed: $SEED_COMPILER" >&2
  exit 2
fi
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
export VPP_HOME="$ROOT_DIR"
# Rebuild the i32 compiler from V++ to install the two pointer-safe AOT
# machine boundaries and runtime-object output mode. The seed uses V++/ASM.
seed_output="$OUTPUT_DIR/vpp-i32"
seed_needs_build=0
if [[ ! -x "$seed_output" || "$SEED_COMPILER" -nt "$seed_output" ]]; then
  seed_needs_build=1
else
  # When resuming after a slow emulated build, reuse the complete Stage 2
  # compiler unless one of its native V++ inputs has changed.
  for input in "$ROOT_DIR/gói/biên dịch/bản địa/"*.vi; do
    if [[ "$input" -nt "$seed_output" ]]; then
      seed_needs_build=1
      break
    fi
  done
fi
if (( seed_needs_build )); then
  echo '[runtime 1/2] Build V++ i32 compiler using Stage 1' >&2
  "$SEED_COMPILER" build "$ROOT_DIR/gói/biên dịch/bản địa/bộ biên dịch.vi" -o "$seed_output"
else
  echo '[runtime 1/2] Reuse current V++ i32 compiler' >&2
fi
runtime_source="$ROOT_DIR/gói/máy ảo/runtime AOT.vi"
if [[ "$(uname -s)" == Linux ]]; then
  # Include the V++ native IEEE-754 conversion implementation in the ELF.
  runtime_source="$ROOT_DIR/gói/máy ảo/runtime syscall.vi"
fi
echo "[runtime 2/2] Compile V++ runtime object: $runtime_source" >&2
"$OUTPUT_DIR/vpp-i32" runtime-object "$runtime_source" -o "$OUTPUT_DIR/runtime.o"
if [[ ! -s "$OUTPUT_DIR/runtime.o" ]]; then
  echo 'V++ runtime-object reported success but did not publish a non-empty runtime.o.' >&2
  exit 1
fi
if [[ "$(uname -s)" == Darwin ]]; then
  if ! file "$OUTPUT_DIR/runtime.o" | grep -q 'Mach-O 64-bit object arm64'; then
    echo 'V++ runtime-object did not produce a Mach-O 64-bit arm64 relocatable.' >&2
    exit 1
  fi
  # Prebuild the platform ABI once; the direct compiler needs no assembler
  # for user programs or subsequent full compiler self-builds.
  /usr/bin/clang -c "$ROOT_DIR/scripts/ASM/macos/vpp_aot_primitives_aarch64.S" \
    -DVPP_NATIVE_RUNTIME_STACK_ABI=1 -o "$OUTPUT_DIR/platform.o"
  /usr/bin/clang -c "$ROOT_DIR/scripts/ASM/macos/vpp_aot_dynamic_bridge_aarch64.S" \
    -o "$OUTPUT_DIR/dynamic-bridge.o"
elif [[ "$(uname -s)" == Linux ]]; then
  # Linux runtime-object produces an ELF64 x86-64 relocatable. Verify the
  # machine format before a future freestanding link consumes this object.
  if ! command -v readelf >/dev/null 2>&1; then
    echo 'readelf is required to verify the Linux runtime object.' >&2
    exit 2
  fi
  header="$(readelf -h "$OUTPUT_DIR/runtime.o")"
  if ! grep -Eq 'Type:[[:space:]]+REL ' <<< "$header" ||
     ! grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64' <<< "$header"; then
    echo 'V++ runtime-object did not produce an ELF64 x86-64 relocatable.' >&2
    exit 1
  fi
fi
echo "V++ AOT runtime: $OUTPUT_DIR/runtime.o"
