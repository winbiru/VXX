#!/usr/bin/env bash
# Ghép nguồn theo chức năng rồi hợp dịch bộ máy Linux x86-64.
# Dùng VPP_LINUX_HOST_COMPILER=/đường/tới/vpp để hợp dịch bằng V++.
# Tệp .o là kết quả dựng; chạy lại sau khi sửa nguồn ASM.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUTPUT="${1:-$ROOT_DIR/scripts/ASM/linux/vpp_aot_primitives_linux_x86_64.o}"
ASSEMBLER="${VPP_LINUX_HOST_ASSEMBLER:-clang}"
mkdir -p "$(dirname "$OUTPUT")"
temporary_dir="$(mktemp -d "${OUTPUT}.tmp.XXXXXXXX")"
trap 'rm -rf "$temporary_dir"' EXIT
temporary="$temporary_dir/host.o"
source_file="$temporary_dir/host.s"

# Trình hợp dịch V++ nhận nguồn phẳng. Danh sách #include chỉ xác định thứ tự
# ghép các phần; không cần bộ tiền xử lý hay đối tượng nhị phân có sẵn.
sed -n 's/^#include "\([^"]*\)"$/\1/p' \
    "$ROOT_DIR/scripts/ASM/linux/vpp_linux_host_x86_64.s" \
    > "$temporary_dir/parts"
[[ -s "$temporary_dir/parts" ]] || {
    echo 'Danh sách nguồn ASM Linux trống.' >&2
    exit 2
}
while IFS= read -r part; do
    cat "$ROOT_DIR/scripts/ASM/linux/$part" >> "$source_file"
    printf '\n' >> "$source_file"
done < "$temporary_dir/parts"

if [[ -n "${VPP_LINUX_HOST_COMPILER:-}" ]]; then
    "$VPP_LINUX_HOST_COMPILER" assemble \
        "$source_file" -o "$temporary"
else
    command -v "$ASSEMBLER" >/dev/null || {
        echo "Không tìm thấy trình hợp dịch LLVM: $ASSEMBLER" >&2
        exit 2
    }
    "$ASSEMBLER" -target x86_64-unknown-linux-gnu -c -x assembler \
        "$source_file" -o "$temporary"
fi

mv -f "$temporary" "$OUTPUT"
echo "Đối tượng ASM Linux x86-64 độc lập: $OUTPUT"
