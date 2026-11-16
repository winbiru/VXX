// Đồng hồ, ngủ và nguồn ngẫu nhiên của hệ thống.

// Dùng chung phép toán đồng hồ, nanosleep có giới hạn và getrandom trên Linux x86-64 bằng syscall
// trực tiếp.
#define VPP_VM_SYSTEM_FREESTANDING 1
#include "../../common/posix/vpp_vm_system_primitives.inc"

.bss
.p2align 3
.globl vpp_native_arena_base
vpp_native_arena_base: .zero 8
vpp_linux_errno: .zero 4
.section .note.GNU-stack,"",@progbits
