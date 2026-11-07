#!/usr/bin/env bash
# Test actual V++ AOT/GC/closures on Ubuntu amd64, preserving the detailed logs.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUTPUT_DIR="${1:-$ROOT_DIR/build/native-dynamic-vpp-ubuntu-docker}"
UBUNTU_IMAGE="${VPP_UBUNTU_IMAGE:-ubuntu:24.04}"
if ! command -v docker >/dev/null 2>&1; then
    echo 'Docker CLI is required.' >&2
    exit 2
fi
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
STATUS_FILE="$OUTPUT_DIR/docker-run.status"
started_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
printf 'RUNNING started_at=%s pid=%s\n' "$started_at" "$$" > "$STATUS_FILE"
finish() {
    local status=$?
    trap - EXIT
    local result=FAIL
    if (( status == 0 )); then
        result=PASS
    fi
    local finished_at
    finished_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf '%s exit=%s started_at=%s finished_at=%s\n' \
        "$result" "$status" "$started_at" "$finished_at" > "$STATUS_FILE"
    printf '[docker] %s (exit=%s, finished_at=%s)\n' \
        "$result" "$status" "$finished_at" | tee -a "$OUTPUT_DIR/docker-run.log"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
set +e
docker run --rm --platform linux/amd64 \
    --mount "type=bind,source=$ROOT_DIR,target=/src,readonly" \
    --mount "type=bind,source=$OUTPUT_DIR,target=/out" \
    --workdir /out --env DEBIAN_FRONTEND=noninteractive \
    --env "VPP_LINUX_STAGE1_TIMEOUT_SECONDS=${VPP_LINUX_STAGE1_TIMEOUT_SECONDS:-1800}" \
    --env "VPP_LINUX_RUNTIME_TIMEOUT_SECONDS=${VPP_LINUX_RUNTIME_TIMEOUT_SECONDS:-3600}" \
    "$UBUNTU_IMAGE" bash -euc '
        test "$(uname -s)-$(uname -m)" = Linux-x86_64
        apt-get update -qq
        apt-get install -y -qq --no-install-recommends cmake make g++ clang binutils libssl-dev
        bash /src/scripts/quality/check_linux_dynamic_vpp.sh /out
    ' 2>&1 | tee "$OUTPUT_DIR/docker-run.log"
status=${PIPESTATUS[0]}
set -e
exit "$status"
