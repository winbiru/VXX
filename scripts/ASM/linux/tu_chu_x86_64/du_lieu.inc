// Hằng và vùng lưu trạng thái riêng của nền tảng.

.section .rodata
Lthu_muc_nha_khoa:
.asciz "VPP_HOME"
Lclang_duong:
.asciz "/usr/bin/clang"
Lchuong_trinh_lien_ket:
.asciz "/proc/self/exe"
.Lmay_tam_bang_chu:
.ascii "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
.data
.p2align 3
vpp_native_arena_base:
.quad 0
.p2align 2
vpp_linux_host_errno:
.long 0
.p2align 2
vpp_linux_host_cache_report_requested:
.long 0
.p2align 3
vpp_linux_host_initial_stack:
.quad 0
.section .note.GNU-stack,"",@progbits
