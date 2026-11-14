#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
output=${1:-"$root/benchmark/reports/native-$(date +%Y%m%d-%H%M%S)"}
compiler=${VPP_EXEC:-"$root/build-pure-vpp/bin/vpp"}
if [[ -e $output ]]; then echo "Refusing to overwrite benchmark evidence: $output" >&2; exit 3; fi
mkdir -p "$output"
output=$(cd "$output" && pwd)
"$compiler" build "$root/benchmark/languages/workloads.vi" -o "$output/vpp-workloads"
python3 - "$root" "$output" <<'PY'
import json,sys
from pathlib import Path
root,out=sys.argv[1:]
Path(out,'manifest.json').write_text(json.dumps({'root':root,'runtimes':[]},indent=2)+'\n')
PY
python3 "$root/scripts/quality/compare_languages_aot.py" --manifest "$output/manifest.json" \
  --aot "$output/vpp-workloads" --runtimes vpp-aot-vpp --output "$output/report.json" \
  --extended --runs "${VPP_BENCH_RUNS:-9}" --target-ms "${VPP_BENCH_TARGET_MS:-60}"
