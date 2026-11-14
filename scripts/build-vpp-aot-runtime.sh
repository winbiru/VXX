#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/scripts/build-vpp-progress.sh"
OUTPUT_DIR="${1:-$ROOT_DIR/build/native-vpp-runtime}"
SEED_COMPILER="${2:-$ROOT_DIR/bootstrap/macos-arm64/vpp-i32}"
# Standalone builds report 0–100%; the CLI bootstrap reserves 10–50%
# of its overall progress for this runtime build.
progress_start=0
progress_span=100
if [[ ${3:-} == cli ]]; then
  progress_start=10
  progress_span=40
fi
runtime_start=$SECONDS
runtime_progress() {
  printf '[Biên dịch V++ %3d%% | %ds] %s\n' "$((progress_start + progress_span * $1 / 100))" "$((SECONDS - runtime_start))" "$2" >&2
}
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) ;;
  Linux-x86_64)
    if [[ $# -lt 2 ]]; then
      echo 'Linux yêu cầu chỉ định bộ biên dịch khởi tạo V++ i32 bản địa.' >&2
      exit 2
    fi
    ;;
  *)
    echo 'Bộ thực thi AOT động của V++ hiện hỗ trợ macOS ARM64 hoặc Linux x86-64.' >&2
    exit 2
    ;;
esac
if [[ ! -x "$SEED_COMPILER" ]]; then
  echo "Không tìm thấy bộ biên dịch khởi tạo i32 có thể thực thi: $SEED_COMPILER" >&2
  exit 2
fi
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
export VPP_HOME="$ROOT_DIR" VPP_NATIVE_BUILD_PROGRESS="${VPP_NATIVE_BUILD_PROGRESS:-1}"
# Rebuild the i32 compiler from V++ to install the two pointer-safe AOT
# machine boundaries and runtime-object output mode. The seed uses V++/ASM.
seed_output="$OUTPUT_DIR/vpp-i32"
seed_needs_build=0
if [[ ! -x "$seed_output" || "$SEED_COMPILER" -nt "$seed_output" ||
      "${BASH_SOURCE[0]}" -nt "$seed_output" ||
      "$ROOT_DIR/scripts/build-vpp-progress.sh" -nt "$seed_output" ]]; then
  seed_needs_build=1
else
  # When resuming after a slow emulated build, reuse the complete Stage 2
  # compiler unless a V++ dependency or an included ASM boundary changed.
  # Match CMake's recursive input set, including transitive imports outside
  # biên dịch/bản địa; checking only the entrypoint's siblings is insufficient.
  while IFS= read -r -d '' input; do
    if [[ "$input" -nt "$seed_output" ]]; then
      seed_needs_build=1
      break
    fi
  done < <(find "$ROOT_DIR/gói" "$ROOT_DIR/scripts/ASM" -type f -print0)
fi
if (( seed_needs_build )); then
  runtime_progress 0 'Biên dịch bộ biên dịch V++ i32 bằng bộ biên dịch giai đoạn 1'
  vpp_timed_command "$SEED_COMPILER" build "$ROOT_DIR/gói/biên dịch/bản địa/bộ biên dịch.vi" -o "$seed_output"
else
  runtime_progress 0 'Sử dụng lại bộ biên dịch V++ i32 hiện có'
fi
runtime_source="$ROOT_DIR/gói/máy ảo/runtime AOT.vi"
if [[ "$(uname -s)" == Linux ]]; then
  # Include the V++ native IEEE-754 conversion implementation in the ELF.
  runtime_source="$ROOT_DIR/gói/máy ảo/runtime syscall.vi"
fi
runtime_progress 25 "Biên dịch tệp đối tượng bộ thực thi V++: $runtime_source"
vpp_timed_command "$OUTPUT_DIR/vpp-i32" runtime-object "$runtime_source" -o "$OUTPUT_DIR/runtime.o"
if [[ ! -s "$OUTPUT_DIR/runtime.o" ]]; then
  echo 'V++ runtime-object báo thành công nhưng không tạo được tệp runtime.o có dữ liệu.' >&2
  exit 1
fi
if [[ "$(uname -s)" == Darwin ]]; then
  if ! file "$OUTPUT_DIR/runtime.o" | grep -q 'Mach-O 64-bit object arm64'; then
    echo 'V++ runtime-object không tạo được tệp đối tượng Mach-O 64-bit ARM64 có thể định vị lại.' >&2
    exit 1
  fi
  # Prebuild the platform ABI once; the direct compiler needs no assembler
  # for user programs or subsequent full compiler self-builds.
  runtime_progress 75 'Hợp dịch ABI nền tảng và cầu nối động'
  /usr/bin/clang -c "$ROOT_DIR/scripts/ASM/macos/vpp_aot_primitives_aarch64.S" \
    -DVPP_NATIVE_RUNTIME_STACK_ABI=1 -o "$OUTPUT_DIR/platform.o"
  /usr/bin/clang -c "$ROOT_DIR/scripts/ASM/macos/vpp_aot_dynamic_bridge_aarch64.S" \
    -o "$OUTPUT_DIR/dynamic-bridge.o"
elif [[ "$(uname -s)" == Linux ]]; then
  # Linux runtime-object produces an ELF64 x86-64 relocatable. Verify the
  # machine format before a future freestanding link consumes this object.
  runtime_progress 75 'Kiểm tra tệp đối tượng ELF của bộ thực thi Linux'
  if ! command -v readelf >/dev/null 2>&1; then
    echo 'Cần readelf để kiểm tra tệp đối tượng bộ thực thi Linux.' >&2
    exit 2
  fi
  header="$(readelf -h "$OUTPUT_DIR/runtime.o")"
  if ! grep -Eq 'Type:[[:space:]]+REL ' <<< "$header" ||
     ! grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64' <<< "$header"; then
    echo 'V++ runtime-object không tạo được tệp đối tượng ELF64 x86-64 có thể định vị lại.' >&2
    exit 1
  fi
fi
echo "Bộ thực thi AOT V++: $OUTPUT_DIR/runtime.o"
# The parent audits the rebuilt i32 compiler before marking this phase complete.
if [[ ${3:-} != cli ]]; then
  runtime_progress 100 'Biên dịch bộ thực thi hoàn tất'
fi
