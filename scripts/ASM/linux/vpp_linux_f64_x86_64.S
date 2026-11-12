// CPU thực hiện số học IEEE-754. vm_f64_parse/text trong runtime syscall.vi chuyển đổi số thập phân
// bằng V++.
#if !defined(__linux__) || !defined(__x86_64__)
#error Phép toán f64 độc lập chỉ hỗ trợ Linux x86-64
#endif
#define VPP_VM_F64_FREESTANDING 1
#include "../common/posix/vpp_vm_f64_primitives.inc"
.section .note.GNU-stack,"",@progbits
