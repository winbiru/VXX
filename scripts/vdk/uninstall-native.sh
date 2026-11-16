#!/usr/bin/env bash
# Remove only unchanged files recorded at installation/package creation.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
manifest="$root/vpp-install-manifest.tsv"
[[ -f $manifest && ! -L $manifest ]] || { echo 'Không tìm thấy danh sách tệp cài đặt V++.' >&2; exit 2; }
if command -v sha256sum >/dev/null; then hash() { sha256sum < "$1"; }; else hash() { shasum -a 256 < "$1"; }; fi
paths=() hashes=()
while IFS= read -r line || [[ -n $line ]]; do
  digest=${line%%$'\t'*}
  rel=${line#*$'\t'}
  [[ $line == *$'\t'* && $rel != *$'\t'* && $rel != *$'\r'* ]] || { echo 'Danh sách tệp cài đặt không hợp lệ.' >&2; exit 2; }
  if [[ ! $digest =~ ^[0-9a-f]{64}$ || -z $rel || $rel == /* || $rel == *\\* || /$rel/ == *'/../'* || /$rel/ == *'/./'* || /$rel/ == *'//'* || $rel == vpp-install-manifest.tsv ]]; then
    echo 'Danh sách tệp cài đặt không hợp lệ.' >&2; exit 2
  fi
  part=$root
  IFS=/ read -r -a components <<< "$rel"
  for component in "${components[@]}"; do
    part="$part/$component"
    [[ ! -L $part ]] || { echo "Không gỡ qua liên kết: $rel" >&2; exit 2; }
  done
  for previous in "${paths[@]+"${paths[@]}"}"; do
    [[ $previous != "$rel" ]] || { echo 'Danh sách tệp cài đặt trùng lặp.' >&2; exit 2; }
  done
  paths+=("$rel"); hashes+=("$digest")
done < "$manifest"
[[ ${#paths[@]} -gt 0 ]] || { echo 'Danh sách tệp cài đặt trống.' >&2; exit 2; }
kept=0
for ((i=0;i<${#paths[@]};i++)); do
  rel=${paths[i]}; target="$root/$rel"
  [[ -e $target ]] || continue
  if [[ ! -f $target ]] || [[ $(hash "$target" | cut -d ' ' -f 1) != "${hashes[i]}" ]]; then
    printf 'Giữ tệp đã thay đổi: %s\n' "$rel"; kept=1; continue
  fi
  rm -- "$target"
done
for rel in "${paths[@]}"; do
  parent=$(dirname "$root/$rel")
  while [[ $parent != "$root" ]]; do
    rmdir "$parent" 2>/dev/null || break
    parent=$(dirname "$parent")
  done
done
if (( kept )); then echo 'Các tệp đã thay đổi được giữ nguyên.'; fi
rm -- "$manifest"
echo "Đã gỡ các tệp V++ đã cài tại: $root"
