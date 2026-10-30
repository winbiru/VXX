#!/bin/sh
# Observe OS state independently of values returned by the guest VM.
set -eu
mode=$1
shift
case "$mode" in
  mode)
    mask=$(umask)
    case "$(uname -s)" in
      Darwin) actual=$(stat -f '%Lp' "$1") ;;
      *) actual=$(stat -c '%a' "$1") ;;
    esac
    test "$((0$actual))" -eq "$((0777 & ~0$mask))"
    ;;
  pid|credentials)
    vm=$1
    image=$2
    workdir=$3
    cd "$workdir"
    output=$(mktemp "$workdir/vm-os.XXXXXX")
    errors="$output.err"
    expected="$output.expected"
    trap 'rm -f "$output" "$errors" "$expected"' EXIT
    PATH= "$vm" "$image" >"$output" 2>"$errors" &
    child=$!
    if [ "$mode" = pid ]; then
      printf '%s\n%s\n' "$child" "$$" >"$expected"
    else
      { id -ru; id -u; id -rg; id -g; } >"$expected"
    fi
    wait "$child"
    test ! -s "$errors"
    diff -u "$expected" "$output"
    ;;
  *) exit 2 ;;
esac
