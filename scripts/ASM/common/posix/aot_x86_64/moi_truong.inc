// Tìm và chép giá trị môi trường trong giới hạn vùng nhớ.

// Chép chuỗi kết thúc NUL vào ô 4096 byte; trả độ dài hoặc -1. rdi là đích, rsi là nguồn; các thanh
// ghi tạm có thể bị ghi đè.
Lchep_duong:
    xorl %eax, %eax
1:  cmpl $4095, %eax
    jae 2f
    movb (%rsi,%rax), %cl
    movb %cl, (%rdi,%rax)
    testb %cl, %cl
    je 3f
    incl %eax
    jmp 1b
2:  movl $-1, %eax
3:  ret

#ifndef __APPLE__
// Lấy envp từ ngăn xếp ban đầu của nhân, cùng nguồn với argc/argv. Khi dùng _start riêng, đọc
// vpp_linux_envp thay vì environ của libc.
Llinux_ban_dau_envp:
#ifdef VPP_LINUX_HOST_NO_CRT
    movq vpp_linux_host_initial_stack(%rip), %rax
#else
    movq __libc_stack_end@GOTPCREL(%rip), %rax
    movq (%rax), %rax
#endif
    movq (%rax), %rdx
    leaq 16(%rax,%rdx,8), %rax
    ret

// Tìm tên trong môi trường kế thừa bằng cách duyệt trực tiếp.
Llinux_moi_truong_gia_tri:
    call Llinux_ban_dau_envp
    movq %rax, %r8
    testq %r8, %r8
    je .Lmay_moi_truong_thieu
.Lmay_moi_truong_tiep:
    movq (%r8), %r10
    testq %r10, %r10
    je .Lmay_moi_truong_thieu
    xorl %ecx, %ecx
.Lmay_moi_truong_so_sanh:
    movb (%rdi,%rcx), %al
    testb %al, %al
    je .Lmay_moi_truong_khoa_cuoi
    cmpb %al, (%r10,%rcx)
    jne .Lmay_moi_truong_bo_qua
    incq %rcx
    jmp .Lmay_moi_truong_so_sanh
.Lmay_moi_truong_khoa_cuoi:
    cmpb $'=', (%r10,%rcx)
    je .Lmay_moi_truong_tim_thay
.Lmay_moi_truong_bo_qua:
    addq $8, %r8
    jmp .Lmay_moi_truong_tiep
.Lmay_moi_truong_tim_thay:
    leaq 1(%r10,%rcx), %rax
    ret
.Lmay_moi_truong_thieu:
    xorl %eax, %eax
    ret
#endif

.globl PUBLIC(vpp_native_os_env_copy)
PUBLIC(vpp_native_os_env_copy):
    VAO_BO_NHO
    KIEM_TRA_DUONG %edi
    KIEM_TRA_DUONG %esi
    leaq (%r11,%rsi), %rax
    movq %rax, -32(%rbp)
    leaq (%r11,%rdi), %rdi
#ifdef __APPLE__
    call LIB(getenv)
#else
    call Llinux_moi_truong_gia_tri
#endif
    testq %rax, %rax
    je Lbo_nho_loi
    movq %rax, %rsi
    movq -32(%rbp), %rdi
    call Lchep_duong
    TRA_BO_NHO

