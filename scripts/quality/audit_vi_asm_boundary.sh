#!/bin/sh
# Read-only source/dependency inventory. Migrated network and process bindings
# must stay in .vi -> machine opcodes and not silently return to libffi.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
module="$root/gói/hệ thống/tiến trình.vi"
sleep_module="$root/gói/hệ thống/nền tảng.vi"
mono_module="$root/gói/thời gian/đơn điệu.vi"
utc_module="$root/gói/thời gian/giờ quốc tế.vi"
entropy_module="$root/gói/lõi/nguồn ngẫu nhiên.vi"
environment_module="$root/gói/hệ thống/môi trường.vi"
local_module="$root/gói/thời gian/địa phương.vi"
directory_module="$root/gói/nhập xuất/thư mục.vi"
path_module="$root/gói/nhập xuất/đường dẫn.vi"
socket_module="$root/gói/mạng/ổ cắm.vi"
ip_module="$root/gói/mạng/địa chỉ mạng.vi"
hosts_module="$root/gói/mạng/bảng tên máy.vi"
dns_config_module="$root/gói/mạng/cấu hình phân giải tên miền.vi"
dns_connect_module="$root/gói/mạng/kết nối phân giải tên miền.vi"
dns_wire_module="$root/gói/mạng/gói tin tên miền.vi"
dns_module="$root/gói/mạng/phân giải tên miền.vi"

count_matches() {
    find "$root/gói" -type f -name '*.vi' -exec grep -h -E "$1" {} + | wc -l | tr -d '[:space:]'
}

# Inspect executable source lines, not comments. Machine primitives use the
# Vietnamese identifiers of the current V++ ABI (including spaces); checking
# the former underscore spellings silently broke the boundary audit.
has_call() {
    grep -E -v '^[[:space:]]*//' "$1" | grep -Fq "$2"
}

require_call() {
    if ! has_call "$1" "$2"; then
        printf 'FAIL: missing machine binding %s in %s\n' "$2" "$1" >&2
        exit 1
    fi
}

foreign_functions=$(count_matches '^[[:space:]]*ngoại hàm ')
foreign_libraries=$(count_matches '^[[:space:]]*ngoại thư viện ')
printf 'V++ foreign library declarations: %s\n' "$foreign_libraries"
printf 'V++ foreign function declarations: %s (before identity migration: 51)\n' "$foreign_functions"

# The standard library no longer declares FFI; legacy native adapters remain
# for compatibility and OS services until replaced by audited low-level ABI.
test "$foreign_functions" -eq 0 || {
    echo 'FAIL: V++ foreign function declarations reintroduced' >&2
    exit 1
}
test "$foreign_libraries" -eq 0 || {
    echo 'FAIL: V++ foreign library declarations reintroduced' >&2
    exit 1
}
if grep -E '^[[:space:]]*ngoại (hàm|thư viện) ' "$module"; then
    echo 'FAIL: Process FFI declarations were reintroduced' >&2
    exit 1
fi
for operation in tạo 'đối số' 'lệnh windows' 'môi trường' 'bắt đầu' 'thăm dò' đọc đợi lỗi đóng; do
    require_call "$module" "máy tiến trình ${operation}("
done
for operation in mở tiếp đóng; do
    require_call "$socket_module" "máy bộ phân giải ${operation}("
done
for operation in 'nâng cấp' gửi nhận; do
    require_call "$socket_module" "máy tls ${operation}("
done
if grep -E '^[[:space:]]*ngoại hàm socket_(loại|đóng|timeout|gửi|nhận|chấp_nhận|lắng_nghe|kết_nối)_ffi|^[[:space:]]*ngoại thư viện c_socket_posix' "$socket_module"; then
    echo 'FAIL: migrated socket FFI declarations were reintroduced' >&2
    exit 1
fi
require_call "$socket_module" 'máy đầu nối loại(mã đầu nối)'
require_call "$socket_module" 'máy đầu nối đóng(mã đầu nối)'
require_call "$socket_module" 'máy đầu nối thời gian chờ(mã đầu nối, thời gian chờ)'
require_call "$socket_module" 'máy đầu nối gửi(mã đầu nối, đệm, máy ảo lấy độ dài(đệm), 0)'
require_call "$socket_module" 'máy đầu nối nhận(mã đầu nối, đệm, mức trần, 0)'
require_call "$socket_module" 'máy đầu nối chấp nhận(mã đầu nối lắng nghe)'
require_call "$socket_module" 'máy đầu nối lắng nghe(cổng, hàng đợi chờ)'
require_call "$socket_module" 'máy đầu nối kết nối('
if grep -E '^[[:space:]]*ngoại (hàm|thư viện) ' "$dns_wire_module"; then
    echo 'FAIL: DNS wire codec reintroduced FFI' >&2
    exit 1
fi
require_call "$dns_module" 'máy đầu nối kết nối('
require_call "$dns_module" 'phân giải tên miền đọc phản hồi đầy đủ('
# Numeric IPv4/IPv6 literals should be parsed in V++ before invoking the
# compatibility OS name resolver. Connect deadline uses the machine clock.
if ! has_call "$socket_module" 'đầu nối địa chỉ mạng các byte(tên máy)' ||
   ! grep -q 'machineMonotonicMilliseconds' "$root/src/runtime/socket_machine.cpp"; then
    echo 'FAIL: socket literal or monotonic machine boundary missing' >&2
    exit 1
fi
if grep -E '^[[:space:]]*ngoại (hàm|thư viện) ' "$ip_module" "$hosts_module"; then
    echo 'FAIL: V++ IP parser or hosts reintroduced FFI' >&2
    exit 1
fi
if ! has_call "$socket_module" 'bộ.địa chỉ mạng các byte(địa chỉ mạng)' ||
   ! has_call "$hosts_module" 'bộ.địa chỉ mạng các byte(địa chỉ mạng)' ||
   ! has_call "$socket_module" 'bảng tên máy.phân giải tên miền đọc bảng tên máy(tên máy, đường bảng tên máy)' ; then
    echo 'FAIL: shared IP parser or V++ hosts socket resolver missing' >&2
    exit 1
fi
if grep -E '^[[:space:]]*ngoại (hàm|thư viện) |nhập gói/mạng/ổ cắm;' "$dns_config_module"; then
    echo 'FAIL: DNS config must use the shared V++ IP parser without Socket/FFI' >&2
    exit 1
fi
if ! has_call "$dns_config_module" 'nhập gói/mạng/địa chỉ mạng;' ||
   ! has_call "$dns_config_module" 'bộ.địa chỉ mạng các byte(địa chỉ)' ||
   ! grep -q 'VPP_SOCKET_VI_ONLY' "$socket_module"; then
    echo 'FAIL: DNS config parser or strict V++ socket resolver boundary missing' >&2
    exit 1
fi
if grep -E '^[[:space:]]*ngoại (hàm|thư viện) ' "$dns_connect_module"; then
    echo 'FAIL: DNS socket connector reintroduced FFI' >&2
    exit 1
fi
if ! has_call "$dns_connect_module" 'trả về phân giải tên miền phân giải(tên máy)' ||
   ! has_call "$dns_connect_module" 'máy đầu nối kết nối(các byte, cổng, giao vận gói tin, thời gian chờ)' ||
   ! has_call "$dns_connect_module" 'máy đầu nối đóng(mã)' ||
   ! grep -q 'cần cấu hình DNS V++ hoặc tệp hosts' "$dns_connect_module"; then
    echo 'FAIL: native DNS-to-Socket V++ path or cleanup is missing' >&2
    exit 1
fi
if grep -E 'ngoại thư viện c_hệ_thống_tiến_trình|ngoại hàm (getpid|getppid|getuid|geteuid|getgid|getegid)_hệ_thống_ffi' "$module"; then
    echo 'FAIL: migrated identity FFI was reintroduced' >&2
    exit 1
fi
if grep -E '^[[:space:]]*ngoại (hàm|thư viện) ' "$directory_module"; then
    echo 'FAIL: directory module reintroduced FFI' >&2
    exit 1
fi
for operation in tạo xóa mở kế đóng; do
    require_call "$directory_module" "máy thư mục $operation("
done
if grep -E '^[[:space:]]*ngoại (hàm|thư viện) ' "$path_module"; then
    echo 'FAIL: path module reintroduced FFI' >&2
    exit 1
fi
if ! has_call "$path_module" 'máy đường loại(đường, 1)' ||
   ! has_call "$path_module" 'máy đường loại(đường, 0)'; then
    echo 'FAIL: missing path machine primitive bindings' >&2
    exit 1
fi
if grep -E 'ngoại hàm (ngủ_vi_giây_ffi|mã_errno_ffi|mã_eintr_ffi)' "$sleep_module"; then
    echo 'FAIL: migrated sleep FFI was reintroduced' >&2
    exit 1
fi
if ! has_call "$sleep_module" 'máy ngủ mili giây(cần ngủ)'; then
    echo 'FAIL: sleep module no longer calls the machine primitive' >&2
    exit 1
fi
if grep -E 'ngoại hàm đọc_đồng_hồ_(đơn_điệu|utc)_ffi|ngoại thư viện c_(đơn_điệu|utc)' "$mono_module" "$utc_module"; then
    echo 'FAIL: migrated clock FFI was reintroduced' >&2
    exit 1
fi
if ! has_call "$mono_module" 'máy đồng hồ đơn điệu()' ||
   ! has_call "$utc_module" 'máy đồng hồ giờ quốc tế()'; then
    echo 'FAIL: clock module no longer calls machine primitive' >&2
    exit 1
fi
if grep -E 'ngoại hàm lấy_entropy_ffi|ngoại thư viện c_entropy' "$entropy_module"; then
    echo 'FAIL: entropy getentropy FFI was reintroduced' >&2
    exit 1
fi
if ! has_call "$entropy_module" 'máy nguồn ngẫu nhiên các byte(số đọc)'; then
    echo 'FAIL: entropy module no longer calls machine primitive' >&2
    exit 1
fi
if grep -E 'ngoại hàm getenv_hệ_thống_ffi|ngoại thư viện c_hệ_thống' "$environment_module"; then
    echo 'FAIL: environment getenv FFI was reintroduced' >&2
    exit 1
fi
if ! has_call "$environment_module" 'máy môi trường đọc(tên)'; then
    echo 'FAIL: environment module no longer calls machine primitive' >&2
    exit 1
fi
if grep -E 'ngoại hàm đổi_giờ_địa_phương_ffi|ngoại thư viện c_địa_phương' "$local_module"; then
    echo 'FAIL: localtime FFI was reintroduced' >&2
    exit 1
fi
if ! has_call "$local_module" 'máy đồng hồ địa phương(giây byte)'; then
    echo 'FAIL: localtime no longer calls machine primitive' >&2
    exit 1
fi

for identity in 'mã tiến trình' 'mã tiến trình cha' 'mã người dùng' 'mã người dùng hiệu lực' 'mã nhóm' 'mã nhóm hiệu lực'; do
    require_call "$module" "máy tiến trình ${identity}()"
done

cpp_sources=$(find "$root/src" -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \) | wc -l | tr -d '[:space:]')
ffi_symbols=$(grep -E -o 'ffi_call|dlopen|dlsym' "$root/src/runtime/foreign.cpp" | wc -l | tr -d '[:space:]')
asm_calls=$(find "$root/scripts/ASM" -type f \( -name '*.S' -o -name '*.asm' -o -name '*.inc' \) -exec grep -h -E '^[[:space:]]*(bl|blr|call|callq)[[:space:]]+' {} + | wc -l | tr -d '[:space:]')
printf 'Remaining C/C++ source files in src/: %s\n' "$cpp_sources"
printf 'foreign.cpp references to ffi_call/dlopen/dlsym: %s\n' "$ffi_symbols"
printf 'ASM call instructions requiring boundary review: %s\n' "$asm_calls"

binary=${VPP_AUDIT_BINARY:-"$root/build/bin/vpp-cli"}
if test -f "$binary"; then
    printf 'Dynamic dependencies of %s:\n' "$binary"
    if command -v otool >/dev/null 2>&1; then
        otool -L "$binary"
    elif command -v ldd >/dev/null 2>&1; then
        ldd "$binary"
    fi
fi
echo 'PASS: identity, sleep, clock, entropy, environment, localtime, directory and path machine binding ratchets'
