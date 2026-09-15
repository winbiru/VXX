#!/usr/bin/env sh
set -eu

BUNDLE_DIR=${1:-dist}
BUNDLE_DIR=$(CDPATH= cd -- "$BUNDLE_DIR" && pwd)
WORK_ROOT=${2:-"${TMPDIR:-/tmp}/vpp-release-smoke-$$"}

cleanup() {
  rm -rf "$WORK_ROOT"
}
trap cleanup EXIT HUP INT TERM

HOME_DIR="$WORK_ROOT/home"
INSTALL_DIR="$WORK_ROOT/install"
PROJECT_ROOT="$WORK_ROOT/project"
mkdir -p "$HOME_DIR" "$PROJECT_ROOT"

HOME="$HOME_DIR" \
SHELL=/bin/sh \
VPP_INSTALL_DIR="$INSTALL_DIR" \
VPP_SKIP_PROFILE=1 \
"$BUNDLE_DIR/install-vpp.sh"

VPP="$INSTALL_DIR/vpp"
VPP_HOME="$INSTALL_DIR" "$VPP" phiên bản

# Mô phỏng update trên cùng prefix. File stale không còn trong bundle mới phải
# biến mất vì installer thay toàn bộ các thư mục managed.
printf 'stale\n' > "$INSTALL_DIR/templates/.stale-from-old-release"
HOME="$HOME_DIR" \
SHELL=/bin/sh \
VPP_INSTALL_DIR="$INSTALL_DIR" \
VPP_SKIP_PROFILE=1 \
"$BUNDLE_DIR/install-vpp.sh"
if [ -e "$INSTALL_DIR/templates/.stale-from-old-release" ]; then
  echo "Update vẫn để lại file stale từ release cũ." >&2
  exit 1
fi

cd "$PROJECT_ROOT"
VPP_HOME="$INSTALL_DIR" "$VPP" khởi tạo ứng dụng smoke-app
cd smoke-app
VPP_HOME="$INSTALL_DIR" "$VPP" dựng src/main.vi
VPP_HOME="$INSTALL_DIR" "$VPP" chạy src/main.vi
VPP_HOME="$INSTALL_DIR" "$VPP" kiểm thử tests

cd "$INSTALL_DIR/examples/hoa-don-cua-hang"
VPP_HOME="$INSTALL_DIR" "$VPP" dựng src/main.vi
VPP_HOME="$INSTALL_DIR" "$VPP" kiểm thử tests

echo "Release install smoke passed."
