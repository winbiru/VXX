// Định danh tiến trình và thao tác thư mục.

.text
.globl vpp_native_os_identity
vpp_native_os_identity:
    cmpl $5, %edi
    ja Lvm_dinh_danh_sai_x86
    movl $39, %eax
    testl %edi, %edi
    jz 1f
    movl $110, %eax
    cmpl $1, %edi
    je 1f
    movl $102, %eax
    cmpl $2, %edi
    je 1f
    movl $107, %eax
    cmpl $3, %edi
    je 1f
    movl $104, %eax
    cmpl $4, %edi
    je 1f
    movl $108, %eax
1:
    syscall
2:
    ret
Lvm_dinh_danh_sai_x86:
    movl $-22, %eax
    ret
.globl vpp_native_os_directory
vpp_native_os_directory:
    pushq %rbp
    movq %rsp, %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $32, %rsp
    movl %edi, -36(%rbp)
    movl %esi, -40(%rbp)
    movl %edx, -44(%rbp)
    call vpp_native_memory_base
    movq %rax, %r12
    testq %r12, %r12
    jz Lvm_thu_muc_sai_x86
    movl -36(%rbp), %r15d
    movl -40(%rbp), %r13d
    movl -44(%rbp), %r14d
    cmpl $4, %r15d
    ja Lvm_thu_muc_sai_x86
    testl %r15d, %r15d
    jz Lvm_thu_muc_duong_x86
    cmpl $3, %r15d
    jae Lvm_thu_muc_duong_x86
    cmpl $68162544, %r13d
    ja Lvm_thu_muc_sai_x86
    testl $7, %r13d
    jnz Lvm_thu_muc_sai_x86
    addq %r12, %r13
    cmpl $0x44524944, 8(%r13)
    jne Lvm_thu_muc_da_dong_x86
    cmpl $2, %r15d
    je Lvm_thu_muc_dong_x86
    cmpl $68154368, %r14d
    ja Lvm_thu_muc_sai_x86
    addq %r12, %r14
    movq (%r13), %rdi
    movq %r14, %rsi
    movl $8192, %edx
    movl $217, %eax
    syscall
    leaq -32(%rbp), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    leave
    ret
Lvm_thu_muc_dong_x86:
    movq (%r13), %rdi
    movl $0, 8(%r13)
    movq $0, (%r13)
    movl $3, %eax
    syscall
    leaq -32(%rbp), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    leave
    ret
Lvm_thu_muc_duong_x86:
    cmpl $68158464, %r13d
    ja Lvm_thu_muc_sai_x86
    addq %r12, %r13
    movq %r13, %rax
    movl $4096, %ecx
1:
    cmpb $0, (%rax)
    je 2f
    incq %rax
    decl %ecx
    jnz 1b
    jmp Lvm_thu_muc_sai_x86
2:
    cmpl $4, %r15d
    je Lvm_thu_muc_tao_thu_muc_x86
    cmpl $3, %r15d
    je Lvm_thu_muc_xoa_x86
    cmpl $68162544, %r14d
    ja Lvm_thu_muc_sai_x86
    testl $7, %r14d
    jnz Lvm_thu_muc_sai_x86
    addq %r12, %r14
    movl $0, 8(%r14)
    movl $-100, %edi
    movq %r13, %rsi
    movl $0x90000, %edx
    xorl %r10d, %r10d
    movl $257, %eax
    syscall
    testq %rax, %rax
    js Lvm_thu_muc_tra_ve_x86
    movq %rax, (%r14)
    movl $0x44524944, 8(%r14)
    xorl %eax, %eax
Lvm_thu_muc_tra_ve_x86:
    leaq -32(%rbp), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    leave
    ret
Lvm_thu_muc_xoa_x86:
    movl $-100, %edi
    movq %r13, %rsi
    movl $512, %edx
    movl $263, %eax
    syscall
    leaq -32(%rbp), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    leave
    ret
Lvm_thu_muc_tao_thu_muc_x86:
    movl $-100, %edi
    movq %r13, %rsi
    movl $511, %edx
    movl $258, %eax
    syscall
    leaq -32(%rbp), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    leave
    ret
Lvm_thu_muc_da_dong_x86:
    movl $-9, %eax
    leaq -32(%rbp), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    leave
    ret
Lvm_thu_muc_sai_x86:
    movl $-22, %eax
    leaq -32(%rbp), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    leave
    ret
Lvm_thu_muc_nhap_xuat_x86:
    movl $-5, %eax
    leaq -32(%rbp), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    leave
    ret
