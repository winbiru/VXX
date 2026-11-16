#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
build=${VPP_BUILD_DIR:-"$root/build-pure-vpp"}
prefix=${1:-"$HOME/.local"}
cmake -S "$root" -B "$build"
cmake --build "$build" --parallel
cmake --install "$build" --prefix "$prefix"
echo "Installed $prefix/bin/vpp"
