#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
compiler=${1:-"${VPP_EXEC:-$root/build-pure-vpp/bin/vpp}"}
work=$(mktemp -d "${TMPDIR:-/tmp}/vpp-tests.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
# Focused native tests; the remaining historical fixtures are retained for porting.
for name in kiem_tra_aot_i32_run kiem_tra_json_mixed_i32_run kiem_tra_sort_merge kiem_tra_json_byte_view_gc kiem_tra_aot_string_intrinsics; do
  echo "BUILD $name"
  "$compiler" build "$root/src/tests/$name.vi" -o "$work/$name"
  "$work/$name" > "$work/$name.actual"
  diff -u "$root/src/tests/expected/$name.expected" "$work/$name.actual"
  echo "PASS $name"
done
