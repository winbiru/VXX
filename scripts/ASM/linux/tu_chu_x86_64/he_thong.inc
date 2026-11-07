// Đồng hồ, ngủ và nguồn ngẫu nhiên của hệ thống.

.text
.globl vpp_native_os_clock
vpp_native_os_clock:
    pushq %rbp
    movq %rsp, %rbp
    subq $48, %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    call vpp_native_memory_base
    testq %rax, %rax
    jz Lvm_he_thong_sai_x86
    movq %rax, %r10
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    cmpl $1, %edi
    ja Lvm_he_thong_sai_x86
    cmpl $68162544, %esi
    ja Lvm_he_thong_sai_x86
    addq %r10, %rsi
    testl %edi, %edi
    movl $0, %edi
    jnz 1f
    movl $1, %edi
1:
    movl $228, %eax
    syscall
    leave
    ret
.globl vpp_native_os_thread_cpu_clock
vpp_native_os_thread_cpu_clock:
    movl %edi, %esi
    xorl %edi, %edi
    pushq %rbp
    movq %rsp, %rbp
    subq $48, %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    call vpp_native_memory_base
    testq %rax, %rax
    jz Lvm_he_thong_sai_x86
    movq %rax, %r10
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    cmpl $68162544, %esi
    ja Lvm_he_thong_sai_x86
    addq %r10, %rsi
    movl $3, %edi
    movl $228, %eax
    syscall
    leave
    ret
.globl vpp_native_os_sleep
vpp_native_os_sleep:
    pushq %rbp
    movq %rsp, %rbp
    subq $48, %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    call vpp_native_memory_base
    testq %rax, %rax
    jz Lvm_he_thong_sai_x86
    movq %rax, %r10
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    cmpl $999, %edi
    ja Lvm_he_thong_ngu_sai_x86
    testl %edi, %edi
    jz Lvm_he_thong_ngu_hop_le_x86
    imulq $1000000, %rdi, %rax
    movq $0, -32(%rbp)
    movq %rax, -24(%rbp)
    leaq -32(%rbp), %rdi
    xorl %esi, %esi
    movl $35, %eax
    syscall
    testq %rax, %rax
    jns Lvm_he_thong_ngu_hop_le_x86
    negl %eax
    leave
    ret
Lvm_he_thong_ngu_hop_le_x86:
    xorl %eax, %eax
    leave
    ret
Lvm_he_thong_ngu_sai_x86:
    movl $22, %eax
    leave
    ret
.globl vpp_native_os_entropy
vpp_native_os_entropy:
    pushq %rbp
    movq %rsp, %rbp
    subq $48, %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    call vpp_native_memory_base
    testq %rax, %rax
    jz Lvm_he_thong_sai_x86
    movq %rax, %r10
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    leal -1(%rsi), %eax
    cmpl $255, %eax
    ja Lvm_he_thong_sai_x86
    testl %edi, %edi
    js Lvm_he_thong_sai_x86
    movl %edi, %eax
    addq %rsi, %rax
    cmpq $135271424, %rax
    ja Lvm_he_thong_sai_x86
    addq %r10, %rdi
    xorl %edx, %edx
    movl $318, %eax
    syscall
    leave
    ret
Lvm_he_thong_sai_x86:
    movl $-22, %eax
    leave
    ret
