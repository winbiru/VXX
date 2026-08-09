#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

if command -v vpp >/dev/null 2>&1; then
  exec vpp application.vi
fi

ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
exec "${ROOT_DIR}/bin/vpp-cli" application.vi
