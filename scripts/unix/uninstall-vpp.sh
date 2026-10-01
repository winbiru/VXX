#!/usr/bin/env sh
set -eu

INSTALL_DIR="${1:-${VPP_INSTALL_DIR:-$HOME/.local/bin}}"
TARGET_BIN="$INSTALL_DIR/vpp"

normalize_dir() {
  (CDPATH= cd -- "$1" 2>/dev/null && pwd) || printf '%s\n' "$1"
}

INSTALL_DIR=$(normalize_dir "$INSTALL_DIR")

clean_profile() {
  profile=$1
  [ -f "$profile" ] || return 0
  tmp="${profile}.vpp-remove-$$"
  awk -v dir="$INSTALL_DIR" '
    BEGIN { skip = 0; pathline = "export PATH=\"" dir ":$PATH\""; homeline = "export VPP_HOME=\"" dir "\"" }
    $0 == "# >>> VPP installer >>>" { skip = 1; next }
    $0 == "# <<< VPP installer <<<" { skip = 0; next }
    skip { next }
    $0 == "# VPP installer" || $0 == pathline || $0 == homeline { next }
    { print }
  ' "$profile" > "$tmp"
  mv "$tmp" "$profile"
}

for profile in "$HOME/.zshrc" "$HOME/.bashrc" "$HOME/.bash_profile" "$HOME/.profile"; do
  clean_profile "$profile"
done

for managed in vpp gói templates examples install-vpp.sh uninstall-vpp.sh; do
  rm -rf "$INSTALL_DIR/$managed"
done

if [ -d "$INSTALL_DIR" ] && [ -z "$(ls -A "$INSTALL_DIR" 2>/dev/null)" ]; then
  rmdir "$INSTALL_DIR" 2>/dev/null || true
fi

echo "Đã gỡ V++ tại: $INSTALL_DIR"
echo "Đã dọn cấu hình V++ khỏi các profile shell của người dùng."
echo "Các project bên ngoài thư mục cài đặt được giữ nguyên."
