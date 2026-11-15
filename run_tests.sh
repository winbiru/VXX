#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
compiler=${VPP_EXEC:-$root/build-pure-vpp/bin/vpp}
if [[ $# -gt 0 && $1 != --* ]]; then
  compiler=$1
  shift
fi
# An explicit legacy backend or artifact phase still uses the native compiler.
backend=jit
phase=all
args=("$@")
for ((i=0; i<${#args[@]}; i++)); do
  case ${args[i]} in
    --backend) backend=${args[i+1]} ;;
    --backend=*) backend=${args[i]#*=} ;;
    --phase) phase=${args[i+1]} ;;
    --phase=*) phase=${args[i]#*=} ;;
  esac
done
if [[ $backend == jit && $phase == all ]]; then
  exec python3 "$root/scripts/quality/run_native_regression.py" --jit "${VPP_JIT:-$(dirname "$compiler")/vpp-jit}" "$@"
fi
exec python3 "$root/scripts/quality/run_native_regression.py" --compiler "$compiler" "$@"
