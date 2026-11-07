// Linux x86-64: ABI System V, ELF/glibc; SHA-256 bằng ASM độc lập. Hợp dịch: clang -target
// x86_64-unknown-linux-gnu -c <tệp.S>. Con trỏ máy giữ đủ 64 bit.
#include "../common/posix/vpp_aot_primitives_x86_64_unix.inc"
#include "vpp_linux_sha256_x86_64.S"
// Bộ biên dịch khởi động dùng cùng ABI syscall tiến trình với bộ thực thi độc lập. Đối tượng này tự
// chứa phần syscall cần dùng.
#include "vpp_linux_syscalls_x86_64.S"
