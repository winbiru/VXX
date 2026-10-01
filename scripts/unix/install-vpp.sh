#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SOURCE_BIN="$SCRIPT_DIR/vpp"
SOURCE_UNINSTALLER="$SCRIPT_DIR/uninstall-vpp.sh"
if [ ! -f "$SOURCE_BIN" ]; then
  echo "Không tìm thấy vpp trong thư mục giải nén: $SCRIPT_DIR" >&2
  exit 1
fi
if [ ! -f "$SOURCE_UNINSTALLER" ]; then
  echo "Gói release thiếu uninstall-vpp.sh" >&2
  exit 1
fi
chmod +x "$SOURCE_BIN" "$SOURCE_UNINSTALLER"
"$SOURCE_BIN" phiên bản >/dev/null

INSTALL_DIR="${VPP_INSTALL_DIR:-$HOME/.local/bin}"
TARGET_BIN="$INSTALL_DIR/vpp"
SOURCE_STDLIB="$SCRIPT_DIR/gói"
TARGET_STDLIB="$INSTALL_DIR/gói"
SOURCE_TEMPLATES="$SCRIPT_DIR/templates"
TARGET_TEMPLATES="$INSTALL_DIR/templates"
SOURCE_EXAMPLES="$SCRIPT_DIR/examples"
TARGET_EXAMPLES="$INSTALL_DIR/examples"
STAGING_DIR="$INSTALL_DIR/.vpp-install-$$"

cleanup() {
  rm -rf "$STAGING_DIR"
}
trap cleanup EXIT HUP INT TERM

for required_dir in "$SOURCE_STDLIB" "$SOURCE_TEMPLATES" "$SOURCE_EXAMPLES"; do
  if [ ! -d "$required_dir" ]; then
    echo "Gói release thiếu thư mục bắt buộc: $required_dir" >&2
    exit 1
  fi
done

mkdir -p "$INSTALL_DIR"
rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR/gói" "$STAGING_DIR/templates" "$STAGING_DIR/examples"
cp "$SOURCE_BIN" "$STAGING_DIR/vpp"
chmod +x "$STAGING_DIR/vpp"
cp "$SOURCE_UNINSTALLER" "$STAGING_DIR/uninstall-vpp.sh"
chmod +x "$STAGING_DIR/uninstall-vpp.sh"
cp -R "$SOURCE_STDLIB"/. "$STAGING_DIR/gói"/
cp -R "$SOURCE_TEMPLATES"/. "$STAGING_DIR/templates"/
cp -R "$SOURCE_EXAMPLES"/. "$STAGING_DIR/examples"/

# Cài mới và cập nhật dùng cùng contract. Mỗi thư mục managed được thay toàn bộ
# để file đã bị xóa khỏi release không còn sót lại sau update.
mv "$STAGING_DIR/vpp" "$TARGET_BIN"
mv "$STAGING_DIR/uninstall-vpp.sh" "$INSTALL_DIR/uninstall-vpp.sh"
cp "$SCRIPT_DIR/install-vpp.sh" "$INSTALL_DIR/install-vpp.sh"
chmod +x "$INSTALL_DIR/install-vpp.sh"
for managed_dir in gói templates examples; do
  rm -rf "$INSTALL_DIR/$managed_dir"
  mv "$STAGING_DIR/$managed_dir" "$INSTALL_DIR/$managed_dir"
done

PROFILE_FILE=""
if [ "${VPP_SKIP_PROFILE:-0}" != "1" ]; then
  SHELL_NAME="${SHELL##*/}"

  if [ "$SHELL_NAME" = "zsh" ]; then
    PROFILE_FILE="$HOME/.zshrc"
  elif [ "$SHELL_NAME" = "bash" ]; then
    if [ -f "$HOME/.bashrc" ]; then
      PROFILE_FILE="$HOME/.bashrc"
    else
      PROFILE_FILE="$HOME/.bash_profile"
    fi
  else
    PROFILE_FILE="$HOME/.profile"
  fi

  [ -f "$PROFILE_FILE" ] || touch "$PROFILE_FILE"
  PROFILE_TMP="${PROFILE_FILE}.vpp-install-$$"
  awk -v dir="$INSTALL_DIR" '
    BEGIN { skip = 0; pathline = "export PATH=\"" dir ":$PATH\""; homeline = "export VPP_HOME=\"" dir "\"" }
    $0 == "# >>> VPP installer >>>" { skip = 1; next }
    $0 == "# <<< VPP installer <<<" { skip = 0; next }
    skip { next }
    $0 == "# VPP installer" || $0 == pathline || $0 == homeline { next }
    { print }
  ' "$PROFILE_FILE" > "$PROFILE_TMP"
  mv "$PROFILE_TMP" "$PROFILE_FILE"
  {
    echo ""
    echo "# >>> VPP installer >>>"
    echo "export VPP_HOME=\"$INSTALL_DIR\""
    echo "case \":\$PATH:\" in *\":\$VPP_HOME:\"*) ;; *) export PATH=\"\$VPP_HOME:\$PATH\" ;; esac"
    echo "# <<< VPP installer <<<"
  } >> "$PROFILE_FILE"
fi

case ":$PATH:" in
  *":$INSTALL_DIR:"*) ;;
*) export PATH="$INSTALL_DIR:$PATH" ;;
esac
export VPP_HOME="$INSTALL_DIR"

"$TARGET_BIN" phiên bản >/dev/null
"$TARGET_BIN" chẩn đoán >/dev/null

echo "Đã cài đặt/cập nhật V++ tại: $TARGET_BIN"
echo "Đã cài thư viện chuẩn tại: $TARGET_STDLIB"
echo "Đã cài templates tại: $TARGET_TEMPLATES"
echo "Đã cài examples tại: $TARGET_EXAMPLES"
if [ -n "$PROFILE_FILE" ]; then
  echo "Đã cập nhật PATH/VPP_HOME trong: $PROFILE_FILE"
  echo "Mở terminal mới, hoặc chạy: source $PROFILE_FILE"
else
  echo "Đã bỏ qua cập nhật profile theo VPP_SKIP_PROFILE=1."
fi
