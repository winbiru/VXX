// Phép toán máy độc lập cho bộ thực thi i32 V++; liên kết cùng vpp_linux_syscalls_x86_64.S và
// vpp_linux_start_x86_64.S. Dùng cùng ABI công khai với POSIX. Lỗi syscall được đổi thành -1 và lưu
// qua vpp_native_os_errno.
#if !defined(__linux__) || !defined(__x86_64__)
#error Bộ thực thi độc lập chỉ hỗ trợ Linux x86-64
#endif
#define VPP_LINUX_ARENA_SIZE 135271424
#define VPP_LINUX_PATH_MAX 4096

#include "thuc_thi_x86_64/bo_nho.inc"
#include "thuc_thi_x86_64/tep.inc"
#include "thuc_thi_x86_64/moi_truong.inc"
#include "thuc_thi_x86_64/tep_tam.inc"
#include "thuc_thi_x86_64/tien_trinh.inc"
#include "thuc_thi_x86_64/doi_so.inc"
#include "thuc_thi_x86_64/he_thong.inc"
