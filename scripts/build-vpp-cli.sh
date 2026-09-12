#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

mkdir -p bin

# Discover production sources so this shortcut cannot drift from new modules.
# Tests are intentionally excluded; they are run through run_tests.sh.
SOURCES=()
while IFS= read -r source; do
  SOURCES+=("$source")
done < <(find src -type f -name '*.cpp' ! -path 'src/tests/*' -print | sort)
c++ -std=c++17 -Isrc/include "${SOURCES[@]}" -pthread -o bin/vpp-cli
