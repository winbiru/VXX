#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SOURCE_BIN="$SCRIPT_DIR/vpp"
if [ ! -f "$SOURCE_BIN" ]; then
  echo "Không tìm thấy vpp trong thư mục giải nén: $SCRIPT_DIR" >&2
  exit 1
fi

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
cp -R "$SOURCE_STDLIB"/. "$STAGING_DIR/gói"/
cp -R "$SOURCE_TEMPLATES"/. "$STAGING_DIR/templates"/
cp -R "$SOURCE_EXAMPLES"/. "$STAGING_DIR/examples"/

# Cài mới và cập nhật dùng cùng contract. Mỗi thư mục managed được thay toàn bộ
# để file đã bị xóa khỏi release không còn sót lại sau update.
mv "$STAGING_DIR/vpp" "$TARGET_BIN"
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

  if ! grep -Fq "$INSTALL_DIR" "$PROFILE_FILE"; then
    {
      echo ""
      echo "# VPP installer"
      echo "export PATH=\"$INSTALL_DIR:\$PATH\""
    } >> "$PROFILE_FILE"
  fi

  if ! grep -Fq 'export VPP_HOME=' "$PROFILE_FILE"; then
    echo "export VPP_HOME=\"$INSTALL_DIR\"" >> "$PROFILE_FILE"
  fi
fi

case ":$PATH:" in
  *":$INSTALL_DIR:"*) ;;
*) export PATH="$INSTALL_DIR:$PATH" ;;
esac
export VPP_HOME="$INSTALL_DIR"

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
