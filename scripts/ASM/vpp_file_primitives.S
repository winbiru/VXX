// File machine ABI used by V++ tệp thô.vi. No libc symbol calls.
// (number, a, b, c, d, errno_out) -> signed 64-bit syscall result.
// VM/AOT adapters only translate boxed values and owned buffers into this ABI.
#if defined(__APPLE__)
#define FILE_SYSCALL _vpp_machine_file_syscall4
#define SOCKET_OPTION_SYSCALL _vpp_machine_socket_syscall5
#define SOCKET_IO_SYSCALL _vpp_machine_socket_syscall6
#else
#define FILE_SYSCALL vpp_machine_file_syscall4
#define SOCKET_OPTION_SYSCALL vpp_machine_socket_syscall5
#define SOCKET_IO_SYSCALL vpp_machine_socket_syscall6
#endif
.text
.globl FILE_SYSCALL
.p2align 2
FILE_SYSCALL:
#if defined(__aarch64__)
    mov x9, x5
#if defined(__APPLE__)
    mov x16, x0
#else
    mov x8, x0
#endif
    mov x0, x1
    mov x1, x2
    mov x2, x3
    mov x3, x4
#if defined(__APPLE__)
    svc #0x80
    b.cc Lfile_success
    str w0, [x9]
#else
    svc #0
    cmn x0, #4095
    b.cc Lfile_success
    neg w10, w0
    str w10, [x9]
#endif
    mov x0, #-1
    ret
Lfile_success:
    str wzr, [x9]
    ret
#elif defined(__x86_64__)
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    movq %r8, %r10
#if defined(__APPLE__)
    orq $0x2000000, %rax
    syscall
    jnc Lfile_success
#else
    syscall
    cmpq $-4095, %rax
    jb Lfile_success
    negl %eax
#endif
    movl %eax, (%r9)
    movq $-1, %rax
    ret
Lfile_success:
    movl $0, (%r9)
    ret
#else
#error Unsupported V++ file machine ABI
#endif

// (number, a, b, c, d, e, errno_out) -> signed 64-bit result.
// Used by socket setsockopt; preserve the same OS error convention as syscall4.
.globl SOCKET_OPTION_SYSCALL
.p2align 2
SOCKET_OPTION_SYSCALL:
#if defined(__aarch64__)
    mov x9, x6
#if defined(__APPLE__)
    mov x16, x0
#else
    mov x8, x0
#endif
    mov x0, x1
    mov x1, x2
    mov x2, x3
    mov x3, x4
    mov x4, x5
#if defined(__APPLE__)
    svc #0x80
    b.cc Lsocket_option_success
    str w0, [x9]
#else
    svc #0
    cmn x0, #4095
    b.cc Lsocket_option_success
    neg w10, w0
    str w10, [x9]
#endif
    mov x0, #-1
    ret
Lsocket_option_success:
    str wzr, [x9]
    ret
#elif defined(__x86_64__)
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    movq %r8, %r10
    movq %r9, %r8
#if defined(__APPLE__)
    orq $0x2000000, %rax
    syscall
    jnc Lsocket_option_success
#else
    syscall
    cmpq $-4095, %rax
    jb Lsocket_option_success
    negl %eax
#endif
    movq 8(%rsp), %r9
    movl %eax, (%r9)
    movq $-1, %rax
    ret
Lsocket_option_success:
    movq 8(%rsp), %r9
    movl $0, (%r9)
    ret
#else
#error Unsupported V++ socket machine ABI
#endif

// (number, a, b, c, d, e, f, errno_out) -> signed 64-bit result.
// sendto/recvfrom use all six OS arguments to avoid libc socket I/O.
.globl SOCKET_IO_SYSCALL
.p2align 2
SOCKET_IO_SYSCALL:
#if defined(__aarch64__)
    mov x9, x7
#if defined(__APPLE__)
    mov x16, x0
#else
    mov x8, x0
#endif
    mov x0, x1
    mov x1, x2
    mov x2, x3
    mov x3, x4
    mov x4, x5
    mov x5, x6
#if defined(__APPLE__)
    svc #0x80
    b.cc Lsocket_io_success
    str w0, [x9]
#else
    svc #0
    cmn x0, #4095
    b.cc Lsocket_io_success
    neg w10, w0
    str w10, [x9]
#endif
    mov x0, #-1
    ret
Lsocket_io_success:
    str wzr, [x9]
    ret
#elif defined(__x86_64__)
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    movq %r8, %r10
    movq %r9, %r8
    movq 8(%rsp), %r9
#if defined(__APPLE__)
    orq $0x2000000, %rax
    syscall
    jnc Lsocket_io_success
#else
    syscall
    cmpq $-4095, %rax
    jb Lsocket_io_success
    negl %eax
#endif
    movq 16(%rsp), %r9
    movl %eax, (%r9)
    movq $-1, %rax
    ret
Lsocket_io_success:
    movq 16(%rsp), %r9
    movl $0, (%r9)
    ret
#else
#error Unsupported V++ socket machine ABI
#endif
#if defined(__linux__)
#if defined(__aarch64__)
.section .note.GNU-stack,"",%progbits
#else
.section .note.GNU-stack,"",@progbits
#endif
#endif
