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
SOURCE_STDLIB="$SCRIPT_DIR/gói"
TARGET_STDLIB="$INSTALL_DIR/gói"
SOURCE_TEMPLATES="$SCRIPT_DIR/templates"
TARGET_TEMPLATES="$INSTALL_DIR/templates"
SOURCE_EXAMPLES="$SCRIPT_DIR/examples"
TARGET_EXAMPLES="$INSTALL_DIR/examples"

mkdir -p "$INSTALL_DIR"
cp "$SOURCE_BIN" "$TARGET_BIN"
chmod +x "$TARGET_BIN"

if [ ! -d "$SOURCE_STDLIB" ]; then
  echo "Khong tim thay thu vien chuan tai: $SOURCE_STDLIB" >&2
  exit 1
fi
mkdir -p "$TARGET_STDLIB"
cp -R "$SOURCE_STDLIB"/. "$TARGET_STDLIB"/

if [ ! -d "$SOURCE_TEMPLATES" ]; then
  echo "Khong tim thay templates tai: $SOURCE_TEMPLATES" >&2
  exit 1
fi
mkdir -p "$TARGET_TEMPLATES"
cp -R "$SOURCE_TEMPLATES"/. "$TARGET_TEMPLATES"/

if [ ! -d "$SOURCE_EXAMPLES" ]; then
  echo "Khong tim thay examples tai: $SOURCE_EXAMPLES" >&2
  exit 1
fi
mkdir -p "$TARGET_EXAMPLES"
cp -R "$SOURCE_EXAMPLES"/. "$TARGET_EXAMPLES"/

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

if ! grep -Fq 'export VPP_HOME=' "$PROFILE_FILE"; then
  echo "export VPP_HOME=\"$INSTALL_DIR\"" >> "$PROFILE_FILE"
fi

case ":$PATH:" in
  *":$INSTALL_DIR:"*) ;;
*) export PATH="$INSTALL_DIR:$PATH" ;;
esac
export VPP_HOME="$INSTALL_DIR"

echo "Da cai dat VPP tai: $TARGET_BIN"
echo "Da cai dat thu vien chuan tai: $TARGET_STDLIB"
echo "Da cai dat templates tai: $TARGET_TEMPLATES"
echo "Da cai dat examples tai: $TARGET_EXAMPLES"
echo "Da cap nhat PATH trong: $PROFILE_FILE"
echo "Mo terminal moi, hoac chay: source $PROFILE_FILE"
