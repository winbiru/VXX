#!/bin/sh
# V++ validates evidence and computes statistics; preserve the CLI exit contract.
set -eu
root=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)
report=$1
cli=${VPP_EXEC:-"$root/build/bin/vpp-cli"}
out=$(mktemp -d "${TMPDIR:-/tmp}/vpp-benchmark-compare.XXXXXX")
trap 'rm -rf "$out"' EXIT HUP INT TERM
if ! cmake "-DVPP_EXEC=$cli" "-DVPP_ROOT=$root" "-DVPP_REPORT=$report" \
  "-DVPP_OUTPUT_DIR=$out" -P "$root/scripts/quality/compare_benchmarks.cmake"; then
  exit 3
fi
code=$(cat "$out/status")
exit "$code"
