#!/usr/bin/env bash
# Run the freestanding ELF smoke test on Ubuntu Linux/amd64 in Docker.
# Source is mounted read-only; output artifacts are kept in the host build/ dir.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUTPUT_DIR="${1:-$ROOT_DIR/build/native-freestanding-ubuntu-docker}"
UBUNTU_IMAGE="${VPP_UBUNTU_IMAGE:-ubuntu:24.04}"

if ! command -v docker >/dev/null 2>&1; then
    echo 'Docker CLI is required to run the Ubuntu freestanding test.' >&2
    exit 2
fi

mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"

# Preserve Docker's full output so emulator/loader failures can be diagnosed
# separately from failures in V++ itself.
set +e
docker run --rm --platform linux/amd64 \
    --mount "type=bind,source=$ROOT_DIR,target=/src,readonly" \
    --mount "type=bind,source=$OUTPUT_DIR,target=/out" \
    --workdir /out \
    --env DEBIAN_FRONTEND=noninteractive \
    "$UBUNTU_IMAGE" bash -euc '
        test "$(uname -s)" = Linux
        test "$(uname -m)" = x86_64
        apt-get update -qq
        apt-get install -y -qq --no-install-recommends clang binutils
        CC=clang bash /src/scripts/quality/check_linux_freestanding.sh /out
    ' 2>&1 | tee "$OUTPUT_DIR/docker-run.log"
docker_status=${PIPESTATUS[0]}
set -e

if (( docker_status != 0 )); then
    if grep -Fq 'rosetta error: bss_size overflow' "$OUTPUT_DIR/docker-run.log"; then
        cat >&2 <<'EOF'
Docker Desktop's Rosetta emulator failed before the V++ ELF could run.
On Apple Silicon, open Docker Desktop > Settings > General, disable
"Use Rosetta for x86_64/amd64 emulation on Apple Silicon", apply/restart
Docker Desktop, and rerun this script. This switches amd64 execution to
QEMU emulation and does not change V++ source or ELF dependencies.
EOF
    fi
    exit "$docker_status"
fi
