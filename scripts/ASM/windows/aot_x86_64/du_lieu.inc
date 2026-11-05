// Hằng và vùng lưu trạng thái riêng của nền tảng.

.section .rdata,"dr"
Lthu_muc_nha_khoa_utf16: .short 'V', 'P', 'P', '_', 'H', 'O', 'M', 'E', 0
Lsha256_thuat_toan: .short 0x53, 0x48, 0x41, 0x32, 0x35, 0x36, 0
.data
.p2align 3
vpp_native_arena_base: .quad 0
vpp_native_spawn_handle: .quad 0
vpp_native_argument_buffer: .quad 0
vpp_native_argument_index: .long -1

#ifndef VPP_WINDOWS_BOUNDARY_TEST
#include "../../common/vpp_vietnamese_runtime_symbols.inc"
#endif
