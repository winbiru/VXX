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
for operation in tạo đối_số lệnh_windows môi_trường bắt_đầu thăm_dò đọc đợi lỗi đóng; do
    if ! grep -q "máy_tiến_trình_${operation}(" "$module"; then
        echo "FAIL: missing Process machine operation $operation" >&2
        exit 1
    fi
done
for operation in mở tiếp đóng; do
    if ! grep -q "máy_resolver_${operation}(" "$socket_module"; then
        echo "FAIL: missing Resolver machine operation $operation" >&2
        exit 1
    fi
done
for operation in nâng_cấp gửi nhận; do
    if ! grep -q "máy_tls_${operation}(" "$socket_module"; then
        echo "FAIL: missing TLS machine operation $operation" >&2
        exit 1
    fi
done
if grep -E '^[[:space:]]*ngoại hàm socket_(loại|đóng|timeout|gửi|nhận|chấp_nhận|lắng_nghe|kết_nối)_ffi|^[[:space:]]*ngoại thư viện c_socket_posix' "$socket_module"; then
    echo 'FAIL: migrated socket FFI declarations were reintroduced' >&2
    exit 1
fi
if ! grep -q 'máy_socket_loại(mã socket)' "$socket_module" ||
   ! grep -q 'máy_socket_đóng(mã socket)' "$socket_module" ||
   ! grep -q 'máy_socket_timeout(mã socket, thời gian chờ)' "$socket_module" ||
   ! grep -q 'máy_socket_gửi(mã socket, đệm, do_dai(đệm), 0)' "$socket_module" ||
   ! grep -q 'máy_socket_nhận(mã socket, đệm, giới_hạn, 0)' "$socket_module" ||
   ! grep -q 'máy_socket_chấp_nhận(mã socket lắng nghe)' "$socket_module" ||
   ! grep -q 'máy_socket_lắng_nghe(cổng, backlog)' "$socket_module" ||
   ! grep -q 'máy_socket_kết_nối(' "$socket_module"; then
    echo 'FAIL: missing migrated socket machine operations' >&2
    exit 1
fi
if grep -E '^[[:space:]]*ngoại (hàm|thư viện) ' "$dns_wire_module"; then
    echo 'FAIL: DNS wire codec reintroduced FFI' >&2
    exit 1
fi
if ! grep -q 'máy_socket_kết_nối(' "$dns_module" ||
   ! grep -q 'phân giải tên miền đọc phản hồi đầy đủ(' "$dns_module"; then
    echo 'FAIL: DNS UDP V++ machine binding missing' >&2
    exit 1
fi
# Numeric IPv4/IPv6 literals should be parsed in V++ before invoking the
# compatibility OS name resolver. Connect deadline uses the machine clock.
if ! grep -q 'đầu nối địa chỉ mạng các byte(tên_máy)' "$socket_module" ||
   ! grep -q 'machineMonotonicMilliseconds' "$root/src/runtime/socket_machine.cpp"; then
    echo 'FAIL: socket literal or monotonic machine boundary missing' >&2
    exit 1
fi
if grep -E '^[[:space:]]*ngoại (hàm|thư viện) ' "$ip_module" "$hosts_module"; then
    echo 'FAIL: V++ IP parser or hosts reintroduced FFI' >&2
    exit 1
fi
if ! grep -q 'bộ.địa chỉ mạng các byte(ip)' "$socket_module" ||
   ! grep -q 'bộ.địa chỉ mạng các byte(ip)' "$hosts_module" ||
   ! grep -q 'hosts.phân giải tên miền đọc bảng tên máy(tên_máy, đường_hosts)' "$socket_module"; then
    echo 'FAIL: shared IP parser or V++ hosts socket resolver missing' >&2
    exit 1
fi
if grep -E '^[[:space:]]*ngoại (hàm|thư viện) |nhập gói/mạng/ổ cắm;' "$dns_config_module"; then
    echo 'FAIL: DNS config must use the shared V++ IP parser without Socket/FFI' >&2
    exit 1
fi
if ! grep -q 'nhập gói/mạng/địa chỉ mạng;' "$dns_config_module" ||
   ! grep -q 'bộ.địa chỉ mạng các byte(địa_chỉ)' "$dns_config_module" ||
   ! grep -q 'VPP_SOCKET_VI_ONLY' "$socket_module"; then
    echo 'FAIL: DNS config parser or strict V++ socket resolver boundary missing' >&2
    exit 1
fi
if grep -E '^[[:space:]]*ngoại (hàm|thư viện) ' "$dns_connect_module"; then
    echo 'FAIL: DNS socket connector reintroduced FFI' >&2
    exit 1
fi
if ! grep -q 'trả về phân giải tên miền phân giải(tên_máy)' "$dns_connect_module" ||
   ! grep -q 'máy_socket_kết_nối(bytes, cổng, udp, thời_gian_chờ)' "$dns_connect_module" ||
   ! grep -q 'máy_socket_đóng(mã)' "$dns_connect_module" ||
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
    if ! grep -q "máy_thư_mục_$operation(" "$directory_module"; then
        echo "FAIL: missing directory primitive $operation" >&2
        exit 1
    fi
done
if grep -E '^[[:space:]]*ngoại (hàm|thư viện) ' "$path_module"; then
    echo 'FAIL: path module reintroduced FFI' >&2
    exit 1
fi
if ! grep -q 'máy_đường_loại(đường, 1)' "$path_module" ||
   ! grep -q 'máy_đường_loại(đường, 0)' "$path_module"; then
    echo 'FAIL: missing path machine primitive bindings' >&2
    exit 1
fi
if grep -E 'ngoại hàm (ngủ_vi_giây_ffi|mã_errno_ffi|mã_eintr_ffi)' "$sleep_module"; then
    echo 'FAIL: migrated sleep FFI was reintroduced' >&2
    exit 1
fi
if ! grep -q 'máy_ngủ_mili_giây(cần_ngủ)' "$sleep_module"; then
    echo 'FAIL: sleep module no longer calls the machine primitive' >&2
    exit 1
fi
if grep -E 'ngoại hàm đọc_đồng_hồ_(đơn_điệu|utc)_ffi|ngoại thư viện c_(đơn_điệu|utc)' "$mono_module" "$utc_module"; then
    echo 'FAIL: migrated clock FFI was reintroduced' >&2
    exit 1
fi
if ! grep -q 'máy_đồng_hồ_đơn_điệu()' "$mono_module" ||
   ! grep -q 'máy_đồng_hồ_utc()' "$utc_module"; then
    echo 'FAIL: clock module no longer calls machine primitive' >&2
    exit 1
fi
if grep -E 'ngoại hàm lấy_entropy_ffi|ngoại thư viện c_entropy' "$entropy_module"; then
    echo 'FAIL: entropy getentropy FFI was reintroduced' >&2
    exit 1
fi
if ! grep -q 'máy_entropy_bytes(số_đọc)' "$entropy_module"; then
    echo 'FAIL: entropy module no longer calls machine primitive' >&2
    exit 1
fi
if grep -E 'ngoại hàm getenv_hệ_thống_ffi|ngoại thư viện c_hệ_thống' "$environment_module"; then
    echo 'FAIL: environment getenv FFI was reintroduced' >&2
    exit 1
fi
if ! grep -q 'máy_môi_trường_đọc(tên)' "$environment_module"; then
    echo 'FAIL: environment module no longer calls machine primitive' >&2
    exit 1
fi
if grep -E 'ngoại hàm đổi_giờ_địa_phương_ffi|ngoại thư viện c_địa_phương' "$local_module"; then
    echo 'FAIL: localtime FFI was reintroduced' >&2
    exit 1
fi
if ! grep -q 'máy_đồng_hồ_địa_phương(giây_byte)' "$local_module"; then
    echo 'FAIL: localtime no longer calls machine primitive' >&2
    exit 1
fi

for identity in pid ppid uid euid gid egid; do
    if ! grep -q "máy_tiến_trình_${identity}()" "$module"; then
        echo "FAIL: missing .vi machine call for $identity" >&2
        exit 1
    fi
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
