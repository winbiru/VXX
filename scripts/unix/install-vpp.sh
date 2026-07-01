#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SOURCE_BIN="$SCRIPT_DIR/vpp"
if [ ! -f "$SOURCE_BIN" ]; then
  echo "Khong tim thay vpp trong thu muc giai nen: $SCRIPT_DIR" >&2
  exit 1
fi

INSTALL_DIR="${VPP_INSTALL_DIR:-$HOME/.local/bin}"
TARGET_BIN="$INSTALL_DIR/vpp"

mkdir -p "$INSTALL_DIR"
cp "$SOURCE_BIN" "$TARGET_BIN"
chmod +x "$TARGET_BIN"

SHELL_NAME="${SHELL##*/}"
PROFILE_FILE=""

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

case ":$PATH:" in
  *":$INSTALL_DIR:"*) ;;
  *) export PATH="$INSTALL_DIR:$PATH" ;;
esac

echo "Da cai dat VPP tai: $TARGET_BIN"
echo "Da cap nhat PATH trong: $PROFILE_FILE"
echo "Mo terminal moi, hoac chay: source $PROFILE_FILE"
