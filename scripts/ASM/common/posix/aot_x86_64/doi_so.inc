// Kiểm tra chỉ số, lấy độ dài và đọc byte đối số chương trình.

// _start riêng của Linux lưu trực tiếp ngăn xếp nhân. Các tầng biên dịch cũ vẫn đọc ngăn xếp glibc
// cho đến khi bật liên kết không CRT.
.globl PUBLIC(vpp_native_argc)
PUBLIC(vpp_native_argc):
#ifdef __APPLE__
    subq $8, %rsp
    call LIB(_NSGetArgc)
    movl (%rax), %eax
    addq $8, %rsp
#else
#ifdef VPP_LINUX_HOST_NO_CRT
    movq vpp_linux_host_initial_stack(%rip), %rax
#else
    movq __libc_stack_end@GOTPCREL(%rip), %rax
    movq (%rax), %rax
#endif
    movl (%rax), %eax
#endif
    ret
Ldoi_so_con_tro:
    cmpl $0, %edi
    jl 3f
    movl %edi, %r10d
#ifdef __APPLE__
    subq $24, %rsp
    movl %r10d, (%rsp)
    call LIB(_NSGetArgc)
    movl (%rax), %ecx
    cmpl %ecx, (%rsp)
    jae 2f
    call LIB(_NSGetArgv)
    movq (%rax), %rax
    movl (%rsp), %r10d
    movq (%rax,%r10,8), %rax
    addq $24, %rsp
    ret
2:  addq $24, %rsp
#else
#ifdef VPP_LINUX_HOST_NO_CRT
    movq vpp_linux_host_initial_stack(%rip), %rax
#else
    movq __libc_stack_end@GOTPCREL(%rip), %rax
    movq (%rax), %rax
#endif
    cmpl (%rax), %edi
    jae 3f
    movq 8(%rax,%r10,8), %rax
    ret
#endif
3:  xorl %eax, %eax
    ret
.globl PUBLIC(vpp_native_os_argument_length)
PUBLIC(vpp_native_os_argument_length):
    pushq %rbp
    movq %rsp, %rbp
    call Ldoi_so_con_tro
    testq %rax, %rax
    je 2f
    xorl %edx, %edx
1:  cmpl $1048576, %edx
    ja 2f
    cmpb $0, (%rax,%rdx)
    je 3f
    incl %edx
    jmp 1b
3:  movl %edx, %eax
    popq %rbp
    ret
2:  movl $-1, %eax
    popq %rbp
    ret
.globl PUBLIC(vpp_native_os_argument_byte)
PUBLIC(vpp_native_os_argument_byte):
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
1:  cmpb $0, (%rax,%rdx)
    je 2f
    cmpl %ebx, %edx
    je 3f
    incl %edx
    jmp 1b
3:  movzbl (%rax,%rdx), %eax
    jmp 4f
2:  movl $-1, %eax
4:  addq $8, %rsp
    popq %rbx
    popq %rbp
    ret

