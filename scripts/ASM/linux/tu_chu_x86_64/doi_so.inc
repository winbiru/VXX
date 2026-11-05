// Kiểm tra chỉ số, lấy độ dài và đọc byte đối số chương trình.

.globl vpp_native_argc
vpp_native_argc:
    movq vpp_linux_host_initial_stack(%rip), %rax
    movl (%rax), %eax
    ret
Ldoi_so_con_tro:
    cmpl $0, %edi
    jl 3f
    movl %edi, %r10d
    movq vpp_linux_host_initial_stack(%rip), %rax
    cmpl (%rax), %edi
    jae 3f
    movq 8(%rax,%r10,8), %rax
    ret
3:
    xorl %eax, %eax
    ret
.globl vpp_native_os_argument_length
vpp_native_os_argument_length:
    pushq %rbp
    movq %rsp, %rbp
    call Ldoi_so_con_tro
    testq %rax, %rax
    je 2f
    xorl %edx, %edx
1:
    cmpl $1048576, %edx
    ja 2f
    cmpb $0, (%rax,%rdx)
    je 3f
    incl %edx
    jmp 1b
3:
    movl %edx, %eax
    popq %rbp
    ret
2:
    movl $-1, %eax
    popq %rbp
    ret
.globl vpp_native_os_argument_byte
vpp_native_os_argument_byte:
    pushq %rbp
    movq %rsp, %rbp
    pushq %rbx
    subq $8, %rsp
    movl %esi, %ebx
    call Ldoi_so_con_tro
    testq %rax, %rax
    je 2f
    cmpl $1048576, %ebx
    jae 2f
    xorl %edx, %edx
1:
    cmpb $0, (%rax,%rdx)
    je 2f
    cmpl %ebx, %edx
    je 3f
    incl %edx
    jmp 1b
3:
    movzbl (%rax,%rdx), %eax
    jmp 4f
2:
    movl $-1, %eax
4:
    addq $8, %rsp
    popq %rbx
    popq %rbp
    ret
