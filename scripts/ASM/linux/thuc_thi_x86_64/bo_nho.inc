// Share TLS arena primitives with the POSIX boundary; preserve syscall helper macros.
#define PUBLIC(x) x
#define VPP_ARENA_SIZE VPP_LINUX_ARENA_SIZE
#define VPP_PATH_MAX VPP_LINUX_PATH_MAX
#define VPP_MEMORY_SET_ERRNO 1
#include "../../common/posix/aot_x86_64/bo_nho.inc"
.macro VPP_VAO_BO_NHO
    pushq %rbp
    movq %rsp, %rbp
    subq $48, %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    testq %rax, %rax
    je .Lbo_nho_chua_co
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
.endm
.macro VPP_KIEM_TRA_LECH thanh_ghi, do_rong
    cmpl $(VPP_LINUX_ARENA_SIZE - \do_rong), \thanh_ghi
    ja .Lbo_nho_khong_hop_le
.endm
.macro VPP_KIEM_TRA_DUONG thanh_ghi
    VPP_KIEM_TRA_LECH \thanh_ghi, VPP_LINUX_PATH_MAX
    movl \thanh_ghi, %eax
    leaq (%r11,%rax), %rax
    xorl %ecx, %ecx
1:  cmpb $0, (%rax,%rcx)
    je 2f
    incl %ecx
    cmpl $VPP_LINUX_PATH_MAX, %ecx
    jb 1b
    jmp .Lbo_nho_khong_hop_le
2:
.endm
.macro VPP_TRA_BO_NHO
    leave
    ret
.endm
.macro VPP_TRA_SYSCALL
    call .Lchuan_hoa_goi_he_thong
    VPP_TRA_BO_NHO
.endm


.Lbo_nho_khong_hop_le:
    movl $22, %edi
    call Lworker_set_errno
.Lbo_nho_chua_co:
    movl $-1, %eax
    VPP_TRA_BO_NHO
