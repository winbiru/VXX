#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
cd "$ROOT_DIR"

echo "Starting api_project (V++ mode, no Python) using src/tests/api_project/application.properties..."
exec "${ROOT_DIR}/bin/vpp-cli" "${ROOT_DIR}/src/tests/api_project/application.vi"
