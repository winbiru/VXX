#!/usr/bin/env bash
# Chỉ sửa cấu hình UTF-8 của người dùng, không cần sudo.
set -euo pipefail
choose_locale() {
  local available candidate found
  available=$(LC_ALL=C locale -a 2>/dev/null || true)
  for candidate in vi_VN.UTF-8 C.UTF-8 en_US.UTF-8 UTF-8; do
    found=$(printf '%s\n' "$available" | awk -v want="$candidate" 'function norm(s){s=tolower(s); gsub(/-/,"",s); return s} norm($0)==norm(want){print; exit}')
    if [[ -n $found ]]; then printf '%s\n' "$found"; return; fi
  done
  printf '%s\n' "$available" | awk 'tolower($0) ~ /utf-?8$/{print; exit}'
}
mode=${1:-check}
if [[ $mode == locale ]]; then choose_locale; exit 0; fi
utf8=$(choose_locale)
start='# >>> VDK UTF-8 >>>'
end='# <<< VDK UTF-8 <<<'
# The dedicated HOME is also used by packaging tests.
: "${HOME:?HOME chưa được thiết lập}"
profiles=()
shell=${SHELL:-sh}
case ${shell##*/} in
  zsh) profiles+=("${ZDOTDIR:-$HOME}/.zshrc") ;;
  bash)
    profiles+=("$HOME/.bashrc")
    if [[ -f $HOME/.bash_profile ]]; then profiles+=("$HOME/.bash_profile")
    elif [[ -f $HOME/.bash_login ]]; then profiles+=("$HOME/.bash_login")
    else profiles+=("$HOME/.profile"); fi ;;
  *) profiles+=("$HOME/.profile") ;;
esac
# Remove a block only if both delimiters are valid. Never overwrite symlinked profiles.
edit_profile() {
  local profile=$1 tmp
  if [[ -L $profile || ( -e $profile && ! -f $profile ) ]]; then
    printf 'VDK: bỏ qua đường dẫn profile không phải tệp thường: %s\n' "$profile" >&2
    return 1
  fi
  if [[ $mode == remove && ! -f $profile ]]; then return 0; fi
  mkdir -p "$(dirname "$profile")"
  tmp=$(mktemp "$profile.vdk.XXXXXXXX")
  if [[ -f $profile ]]; then
    if ! awk -v start="$start" -v end="$end" '
      $0==start {if (inside || seen) exit 2; inside=1; seen=1; next}
      $0==end {if (!inside) exit 2; inside=0; next}
      !inside {print}
      END {if (inside) exit 2}
    ' "$profile" > "$tmp"; then
      rm -f "$tmp"; printf 'VDK: khối cấu hình không hợp lệ: %s\n' "$profile" >&2; return 1
    fi
  fi
  if [[ $mode == install ]]; then
    # Locale names come from locale -a, but restrict them before writing shell code.
    if [[ ! $utf8 =~ ^[A-Za-z0-9_.@-]+$ ]]; then rm -f "$tmp"; return 1; fi
    {
      printf '%s\n' "$start"
      printf "export LANG='%s'\nexport LC_CTYPE='%s'\n" "$utf8" "$utf8"
      printf '%s\n' 'if [ -n "${LC_ALL:-}" ] && [ "$(locale charmap 2>/dev/null)" != UTF-8 ]; then unset LC_ALL; fi'
      printf '%s\n' "$end"
    } >> "$tmp"
  fi
  if [[ -f $profile ]]; then
    if cmp -s "$profile" "$tmp"; then rm -f "$tmp"; return 0; fi
    # Preserve permissions and the profile itself (including its ACL).
    cat "$tmp" > "$profile"; rm -f "$tmp"
  else
    mv "$tmp" "$profile"
  fi
  printf 'Đã cập nhật: %s\n' "$profile"
}
case $mode in
  check)
    if [[ -z $utf8 ]]; then printf 'VDK: Không tìm thấy locale UTF-8. Hãy cài locale UTF-8 của hệ điều hành.\n' >&2; exit 1; fi
    printf 'Bảng mã hiện tại: %s\nLocale UTF-8 có sẵn: %s\n' "$(locale charmap 2>/dev/null || true)" "$utf8"
    printf 'Tiếng Việt: Trường đại học — Nguyễn, Đắk Lắk, chữ ă â ê ô ơ ư đ.\n'
    for profile in "${profiles[@]}"; do
      if [[ -f $profile ]] && grep -Fxq "$start" "$profile"; then printf 'Đã cài: %s\n' "$profile"
      else printf 'Chưa cài: %s\n' "$profile"; fi
    done
    ;;
  install)
    if [[ -z $utf8 ]]; then printf 'VDK: Hệ thống chưa có locale UTF-8.\n' >&2; exit 1; fi
    for profile in "${profiles[@]}"; do edit_profile "$profile"; done
    printf 'Đã cài cấu hình UTF-8 (%s). Mở terminal mới để áp dụng.\n' "$utf8"
    ;;
  remove)
    # Also clean profiles from previously used supported shells.
    profiles=("$HOME/.bashrc" "$HOME/.bash_profile" "$HOME/.bash_login" "$HOME/.profile" "${ZDOTDIR:-$HOME}/.zshrc")
    for profile in "${profiles[@]}"; do
      if [[ -f $profile ]] && grep -Fxq "$start" "$profile"; then edit_profile "$profile"; fi
    done
    printf 'Đã gỡ cấu hình UTF-8 do VDK quản lý. Mở terminal mới để áp dụng.\n'
    ;;
  *) exit 2 ;;
esac
