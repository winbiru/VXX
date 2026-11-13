#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
compiler=${VPP_EXEC:-$root/build-pure-vpp/bin/vpp}
if [[ $# -gt 0 && $1 != --* ]]; then
  compiler=$1
  shift
fi
exec python3 "$root/scripts/quality/run_native_regression.py" --compiler "$compiler" "$@"
