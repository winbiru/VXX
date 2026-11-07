// Điểm vào ELF Linux x86-64 độc lập. Ngăn xếp ban đầu: argc, argv[], NULL, envp[], NULL, auxv[]. Khi
// gọi vpp_freestanding_main: rdi=argc, rsi=argv, rdx=envp.
#if !defined(__linux__) || !defined(__x86_64__)
#error Mã khởi động chỉ hỗ trợ Linux x86-64
#endif

.text
.p2align 4
.globl _start
.type _start, @function
_start:
    xorq %rbp, %rbp
    movq %rsp, vpp_linux_initial_stack(%rip)
    movq (%rsp), %rdi
    leaq 8(%rsp), %rsi
    leaq 16(%rsp,%rdi,8), %rdx
    movq %rdi, vpp_linux_argc(%rip)
    movq %rsi, vpp_linux_argv(%rip)
    movq %rdx, vpp_linux_envp(%rip)
    andq $-16, %rsp
    call vpp_freestanding_main
    movl %eax, %edi
    movl $231, %eax              // exit_group(status)
    syscall
    ud2
.size _start, .-_start

.bss
.p2align 3
.globl vpp_linux_initial_stack
vpp_linux_initial_stack: .zero 8
.globl vpp_linux_argc
vpp_linux_argc: .zero 8
.globl vpp_linux_argv
vpp_linux_argv: .zero 8
.globl vpp_linux_envp
vpp_linux_envp: .zero 8
.section .note.GNU-stack,"",@progbits
