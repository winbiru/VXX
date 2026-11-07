// Hằng và vùng lưu trạng thái riêng của nền tảng.

.section .rodata
Lthu_muc_nha_khoa: .asciz "VPP_HOME"
Lchuong_trinh_duong: .asciz "/proc/self/exe"
Lclang_duong: .asciz "/usr/bin/clang"
.bss
.p2align 3
vpp_native_arena_base: .skip 8
.section .note.GNU-stack,"",@progbits

#include "../../common/vpp_vietnamese_runtime_symbols.inc"
#include "../../common/posix/vpp_vm_system_primitives.inc"
#include "../../common/posix/vpp_vm_directory_primitives.inc"
#include "../../common/posix/vpp_vm_process_primitives.inc"
#include "../../common/posix/vpp_vm_socket_primitives.inc"
