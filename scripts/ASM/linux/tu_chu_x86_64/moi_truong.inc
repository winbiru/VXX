// Tìm và chép giá trị môi trường trong giới hạn vùng nhớ.

Lchep_duong:
    xorl %eax, %eax
1:
    cmpl $4095, %eax
    jae 2f
    movb (%rsi,%rax), %cl
    movb %cl, (%rdi,%rax)
    testb %cl, %cl
    je 3f
    incl %eax
    jmp 1b
2:
    movl $-1, %eax
3:
    ret
Llinux_ban_dau_envp:
    movq vpp_linux_host_initial_stack(%rip), %rax
    movq (%rax), %rdx
    leaq 16(%rax,%rdx,8), %rax
    ret
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
.globl vpp_native_os_env_copy
vpp_native_os_env_copy:
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    cmpl $(135271424 - 4096), %edi
    ja Lbo_nho_loi
    cmpl $(135271424 - 4096), %esi
    ja Lbo_nho_loi
    leaq (%r11,%rsi), %rax
    movq %rax, -32(%rbp)
    leaq (%r11,%rdi), %rdi
    call Llinux_moi_truong_gia_tri
    testq %rax, %rax
    je Lbo_nho_loi
    movq %rax, %rsi
    movq -32(%rbp), %rdi
    call Lchep_duong
    leave
    ret
