#!/usr/bin/env bash
# Build the Windows x64 full toolchain from the checked-in seeds and run
# only the native LSP CMake checks under Wine (no release archive needed).
set -euo pipefail

if [[ ${1:-} != --inside ]]; then
    root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
    resume=
    if [[ $# -ne 0 ]]; then
        if [[ $# -ne 2 || $1 != --resume-i32 || ! -d $2 ]]; then
            echo 'usage: check_windows_lsp_docker.sh [--resume-i32 <previous-output-dir>]' >&2
            exit 2
        fi
        resume=$(cd "$2" && pwd)
        for name in windows-runtime.obj i32-stage1.exe i32-stage2.exe i32-stage3.exe; do
            test -s "$resume/$name" || { echo "Missing resume file: $resume/$name" >&2; exit 2; }
        done
    fi
    mkdir -p "$root/.tmp"
    out=$(mktemp -d "$root/.tmp/windows-lsp-docker.XXXXXXXX")
    if [[ -n $resume ]]; then
        mkdir "$out/resume-i32"
        cp "$resume/windows-runtime.obj" "$resume"/i32-stage{1,2,3}.exe "$out/resume-i32/"
    fi
    echo "Windows LSP output: $out"
    docker run --rm --init --platform linux/amd64 \
        -v "$root:/repo:ro" -v "$out:/work" -w /repo \
        -e LANG=C.UTF-8 -e LC_ALL=C.UTF-8 \
        ubuntu:24.04 bash /repo/scripts/quality/check_windows_lsp_docker.sh --inside \
        2>&1 | tee "$out/docker.log"
    exit 0
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y --no-install-recommends wine64 ca-certificates curl unzip python3

# Use Windows-hosted tools: the full compiler launches clang itself and the
# LSP check must exercise Windows CMake's .cmd launcher and CRLF handling.
mkdir -p /opt/vpp-tools
cd /opt/vpp-tools
curl -fL --retry 3 -o llvm.zip \
    https://github.com/mstorsjo/llvm-mingw/releases/download/20250528/llvm-mingw-20250528-ucrt-x86_64.zip
echo 'cd7813524da5e5cc747d47fbb483a45d39cdf7e0d8e44a60d17d6dd819290359  llvm.zip' | sha256sum -c -
unzip -q llvm.zip
curl -fL --retry 3 -o cmake.zip \
    https://github.com/Kitware/CMake/releases/download/v4.0.3/cmake-4.0.3-windows-x86_64.zip
echo 'b59a31dfbfa376a4aaea9ff560ff2b29f78ee5f9fb15447fc71ae7bf9fea9379  cmake.zip' | sha256sum -c -
unzip -q cmake.zip
rm llvm.zip cmake.zip

export WINEARCH=win64 WINEPREFIX=/tmp/vpp-windows-lsp WINEDEBUG=-all
wine=/usr/lib/wine/wine64
clang=/opt/vpp-tools/llvm-mingw-20250528-ucrt-x86_64/bin/x86_64-w64-mingw32-clang.exe
cmake=/opt/vpp-tools/cmake-4.0.3-windows-x86_64/bin/cmake.exe
export VPP_AOT_ASSEMBLER="Z:$clang"
export VPP_HOME=Z:/repo VPP_NATIVE_NO_CACHE=1 VPP_NATIVE_TARGET=windows-x86_64
export VPP_WINDOWS_RUNTIME_OBJECT=Z:/work/windows-runtime.obj

run() {
    local label=$1
    shift
    echo "RUN $label"
    "$wine" "$@" 2>&1 | tee "/work/$label.log"
}
assemble() {
    local label=$1 source=$2 output=$3
    shift 3
    run "$label" "$clang" -x assembler-with-cpp -c "$source" "$@" \
        -IZ:/repo/scripts/ASM -IZ:/repo/scripts/ASM/common -o "$output"
}
link() {
    local label=$1 output=$2
    shift 2
    run "$label" "$clang" "$@" -lshell32 -lbcrypt -lws2_32 -liphlpapi -lntdll \
        -Wl,--no-insert-timestamp -Wl,--stack,67108864 -o "$output"
}

cd /repo
(cd bootstrap/windows-x86_64 && sha256sum -c SHA256SUMS)
run toolchain "$clang" --version
assemble i32-boundary Z:/repo/scripts/ASM/windows/vpp_aot_primitives_windows_x86_64.S \
    Z:/work/windows-runtime.obj -DVPP_NATIVE_RUNTIME_STACK_ABI=1 \
    -DVPP_JIT_DISABLED=1 -DVPP_WINDOWS_I32_BOOTSTRAP=1

compiler=/repo/bootstrap/windows-x86_64/vpp-i32.exe
# The pinned seed can emit the previous PE layout. Stage 1 is the source
# bootstrap (as in native-vpp.yml); the following three stages must agree.
first_stage=1
if [[ -d /work/resume-i32 ]]; then
    cmp /work/resume-i32/windows-runtime.obj /work/windows-runtime.obj
    cmp /work/resume-i32/i32-stage2.exe /work/resume-i32/i32-stage3.exe
    cp /work/resume-i32/i32-stage{1,2,3}.exe /work/
    compiler=/work/i32-stage3.exe
    first_stage=4
    echo 'Resuming i32 bootstrap; stage 4 will be rebuilt from current source.'
fi
for ((stage=first_stage; stage<=4; stage++)); do
    run "i32-stage$stage" "$compiler" build \
        'Z:/repo/gói/biên dịch/bản địa/bộ biên dịch.vi' -o "Z:/work/i32-stage$stage.exe"
    compiler="/work/i32-stage$stage.exe"
done
python3 scripts/quality/check_native_executable_layout.py /work/i32-stage{2,3,4}.exe
cmp /work/i32-stage2.exe /work/i32-stage3.exe
cmp /work/i32-stage3.exe /work/i32-stage4.exe
echo WINDOWS_I32_BOOTSTRAP_CONVERGENCE=PASS
i32=$compiler
export VPP_AOT_VPP_OBJECT=Z:/work/runtime.o
export VPP_AOT_CORE_OBJECT=Z:/work/runtime-core.o
export VPP_AOT_PLATFORM_OBJECT=Z:/work/platform.o
runtime_source='Z:/repo/gói/máy ảo/runtime syscall.vi'
run runtime "$i32" runtime-object "$runtime_source" -o Z:/work/runtime.o
run runtime-core "$i32" object "$runtime_source" -o Z:/work/runtime-core.o
platform=Z:/repo/scripts/ASM/windows/vpp_windows_full_runtime_x86_64.S
assemble platform "$platform" Z:/work/platform.o
assemble vm-platform "$platform" Z:/work/vm-platform.o -DVPP_STANDALONE_VM_WORKER=1
assemble raw-platform "$platform" Z:/work/raw-platform.o -DVPP_WINDOWS_RAW_BOUNDARY=1
gzip -dc bootstrap/windows-x86_64/vpp-full.S.gz > /work/seed.S
assemble seed Z:/work/seed.S Z:/work/seed.o -DVPP_AOT_VPP_RUNTIME=1
link full-stage1 Z:/work/vpp-native-stage1.exe Z:/work/seed.o \
    Z:/work/runtime.o Z:/work/runtime-core.o Z:/work/platform.o

for stage in 2 3; do
    run "full-stage$stage" "/work/vpp-native-stage$((stage-1)).exe" build \
        'Z:/repo/gói/biên dịch/dòng lệnh AOT tự chủ.vi' -o "Z:/work/vpp-native-stage$stage.exe"
done
cmp /work/vpp-native-stage2.exe /work/vpp-native-stage3.exe
echo WINDOWS_FULL_BOOTSTRAP_CONVERGENCE=PASS

package=/work/package
home=$package/share/vpp
mkdir -p "$package/bin" "$home/native-vpp-runtime" "$home/scripts"
cp /work/vpp-native-stage3.exe "$package/bin/vpp-native.exe"
cp "$i32" "$package/bin/vpp-i32.exe"
cp /work/windows-runtime.obj "$home/"
cp /work/{runtime,runtime-core,platform,vm-platform,raw-platform}.o "$home/native-vpp-runtime/"
cp -R 'gói' templates "$home/"
cp -R scripts/ASM "$home/scripts/"
cp vpp-language.json vpp-libraries.json "$home/"
cp scripts/windows/vpp-windows-launcher.cmd "$package/bin/vpp.cmd"

run vm-build "$i32" object 'Z:/repo/gói/máy ảo/chính.vi' -o Z:/work/vm.o
link vm-link Z:/work/package/bin/vpp-vm.exe Z:/work/vm.o Z:/work/runtime.o Z:/work/vm-platform.o
compiler=/work/vpp-native-stage3.exe
run dns-provider "$compiler" vm-build 'Z:/repo/gói/máy ảo/nhà cung cấp DNS.vi' \
    -o Z:/work/package/bin/dns-provider.vmb
run jit-build "$compiler" build 'Z:/repo/gói/biên dịch/dòng lệnh JIT.vi' \
    -o Z:/work/package/bin/vpp-jit.exe
run tools-build "$compiler" build Z:/repo/scripts/vdk/vpp-cli.vi \
    -o Z:/work/package/bin/vpp-tools.exe

# Use the packaged compiler/runtime and the same CMake test as Windows CI.
export VPP_HOME=Z:/work/package/share/vpp
export VPP_WINDOWS_RUNTIME_OBJECT="$VPP_HOME/windows-runtime.obj"
export VPP_AOT_VPP_OBJECT="$VPP_HOME/native-vpp-runtime/runtime.o"
export VPP_AOT_CORE_OBJECT="$VPP_HOME/native-vpp-runtime/runtime-core.o"
export VPP_AOT_PLATFORM_OBJECT="$VPP_HOME/native-vpp-runtime/platform.o"
status=0
for mode in format diagnostics; do
    run "lsp-$mode" "$cmake" \
        -DVPP_CLI=Z:/work/package/bin/vpp.cmd \
        -DVPP_NATIVE_COMPILER=Z:/work/package/bin/vpp-native.exe \
        "-DVPP_ROOT=$VPP_HOME" "-DVPP_OUTPUT_DIR=Z:/work/lsp-$mode" \
        "-DVPP_LSP_MODE=$mode" -P Z:/repo/scripts/quality/check_native_lsp.cmake || status=1
done
exit "$status"
