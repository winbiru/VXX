#!/usr/bin/env bash
# Converge the full compiler using the direct V++ ARM64/Mach-O route.
# Stage 1 is the audited pinned seed in CI, or the current-source compiler
# from an ordinary CMake build. Stages 2/3 always rebuild current sources.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
source "$root/scripts/build-vpp-progress.sh"
phase=all
if [[ ${1:-} == --phase ]]; then
  phase=${2:-}
  shift 2
fi
case "$phase" in
  all|stage1|stage2|stage3|convergence|test) ;;
  *) echo "Unknown macOS bootstrap phase: $phase" >&2; exit 2 ;;
esac
if [[ $# != 2 || $(uname -s)-$(uname -m) != Darwin-arm64 ]]; then
  echo 'Usage on macOS ARM64: check_macos_full_bootstrap.sh [--phase PHASE] <cmake-build-dir> <output-dir>' >&2
  exit 2
fi
build=$(cd "$1" && pwd)
if [[ "$phase" == all || "$phase" == stage1 ]]; then
  if [[ -e "$2" || -L "$2" ]]; then
    echo 'A fresh macOS bootstrap output directory is required' >&2
    exit 2
  fi
  mkdir -p "$2"
fi
out=$(cd "$2" && pwd)
for name in ${!VPP_@}; do unset "$name"; done
export VPP_HOME="$root" VPP_NATIVE_NO_CACHE=1 VPP_NATIVE_BUILD_PROGRESS=1
runtime="$build/native-vpp-runtime"
export VPP_AOT_VPP_OBJECT="$runtime/runtime.o" VPP_AOT_PLATFORM_OBJECT="$runtime/platform.o" VPP_AOT_DYNAMIC_BRIDGE_OBJECT="$runtime/dynamic-bridge.o"
export VPP_AOT_NATIVE_MACHO_LINK=1 VPP_AOT_NATIVE_ARM64_OBJECT=1
export VPP_AOT_ASSEMBLER="$out/assembler-must-not-run"
for part in runtime.o platform.o dynamic-bridge.o; do
  test -s "$runtime/$part"
  file "$runtime/$part" | grep -q 'Mach-O 64-bit object arm64'
done
source_file="$root/gói/biên dịch/dòng lệnh AOT tự chủ.vi"
compiler="$build/bin/vpp-native"
check_stage() {
  local stage=$1 executable=$2 fixture
  test -x "$executable"
  file "$executable" | grep -q 'Mach-O 64-bit executable arm64'
  /usr/bin/codesign --verify "$executable"
  test "$(/usr/bin/otool -L "$executable" | tail -n +2 | awk '{print $1}')" = /usr/lib/libSystem.B.dylib
  "$executable" --version
  mkdir -p "$out/stage$stage-fixtures"
  for fixture in native_dynamic_vpp native_aot_private_method native_loop_stack_gc native_path_kind; do
    "$executable" build "$root/scripts/test/fixtures/$fixture.vi" -o "$out/stage$stage-$fixture" > "$out/stage$stage-$fixture.build.log" 2>&1 || {
      cat "$out/stage$stage-$fixture.build.log" >&2; return 1;
    }
    /usr/bin/codesign --verify "$out/stage$stage-$fixture"
    (cd "$out/stage$stage-fixtures" && "$out/stage$stage-$fixture") > "$out/stage$stage-$fixture.stdout.log" 2> "$out/stage$stage-$fixture.stderr.log" || {
      cat "$out/stage$stage-$fixture.stderr.log" >&2; return 1;
    }
    diff -u "$root/scripts/test/expected/$fixture.expected" "$out/stage$stage-$fixture.stdout.log"
  done
}
if [[ "$phase" == all || "$phase" == stage1 ]]; then
  check_stage 1 "$compiler"
fi
for stage in 2 3; do
  if [[ "$phase" != all && "$phase" != "stage$stage" ]]; then continue; fi
  if [[ "$stage" == 3 ]]; then compiler="$out/vpp-native-stage2"; fi
  echo "[Biên dịch V++ macOS đầy đủ] Tự biên dịch giai đoạn $stage"
  next="$out/vpp-native-stage$stage"
  vpp_profile_command "$out/stage$stage-profile.log" "$compiler" build "$source_file" -o "$next" 2>&1 | tee "$out/stage$stage-build.log"
  if [[ "$stage" == 2 ]]; then check_stage "$stage" "$next"; fi
done
if [[ "$phase" == all || "$phase" == convergence ]]; then
  # Both stages use the direct linker and deterministic signing. Require exact
  # equality of the actual binaries, including their Mach-O metadata.
  (cd "$out" && shasum -a 256 vpp-native-stage2 vpp-native-stage3 > convergence.sha256)
  cat "$out/convergence.sha256"
  cmp "$out/vpp-native-stage2" "$out/vpp-native-stage3"
  echo 'MACOS_FULL_BOOTSTRAP_CONVERGENCE=PASS'
fi
if [[ "$phase" == all || "$phase" == test ]]; then
  check_stage 3 "$out/vpp-native-stage3"
fi
