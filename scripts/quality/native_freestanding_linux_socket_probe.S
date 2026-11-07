// Socket contract regression: loopback TCP, UDP, timeout, and bad arguments.
// Intended to run inside the freestanding ELF; no libc or POSIX symbols.
#if !defined(__linux__) || !defined(__x86_64__)
#error Linux x86-64 only
#endif
.macro SOCK_CALL op, offset
    movl $\op, %edi
    movl $\offset, %esi
    call vpp_native_os_socket
.endm

// Arena controls (128 bytes each): listener 90000, timeout accept 90128,
// TCP client 90256, accepted client 90384, UDP receiver 90512,
// UDP sender 90640, invalid arguments 90768. Data buffers 94000+.
// Vùng nhớ của bộ máy Linux x86-64 hiện tại.
#define PROBE_ARENA_SIZE 135271424

.text
.p2align 4
.globl vpp_freestanding_socket_probe
.type vpp_freestanding_socket_probe, @function
vpp_freestanding_socket_probe:
    pushq %rbp
    movq %rsp, %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    subq $72, %rsp
    movl $-1, -32(%rbp)  // listening TCP descriptor
    movl $-1, -36(%rbp)  // connected TCP descriptor
    movl $-1, -40(%rbp)  // accepted TCP descriptor
    movl $-1, -44(%rbp)  // UDP receiving descriptor
    movl $-1, -48(%rbp)  // UDP sending descriptor
    call vpp_native_memory_base
    testq %rax, %rax
    je .Lsp_fail
    movq %rax, %r12

    SOCK_CALL 1, PROBE_ARENA_SIZE
    cmpl $-22, %eax
    jne .Lsp_fail

    // TCP bind to 127.0.0.1:0; op 10 discovers the ephemeral port.
    movl $0, 90008(%r12)
    movl $1000, 90012(%r12)
    movl $1, 90020(%r12)
    movl $0x0100007f, 90032(%r12)
    SOCK_CALL 1, 90000
    testl %eax, %eax
    js .Lsp_fail
    movl %eax, -32(%rbp)
    SOCK_CALL 10, 90000
    cmpl $1, %eax
    jl .Lsp_fail
    cmpl $65535, %eax
    jg .Lsp_fail
    movl %eax, 90264(%r12)
    movl $1000, 90268(%r12)
    movl $4, 90280(%r12)
    movl $0x0100007f, 90288(%r12)
    SOCK_CALL 0, 90256
    testl %eax, %eax
    js .Lsp_fail
    movl %eax, -36(%rbp)

    movl -32(%rbp), %eax
    movl %eax, 90384(%r12)
    movl $1000, 90396(%r12)
    SOCK_CALL 2, 90384
    testl %eax, %eax
    js .Lsp_fail
    movl %eax, -40(%rbp)

    // TCP send/receive and timeout option after accept.
    movl $100, 90396(%r12)
    SOCK_CALL 6, 90384
    testl %eax, %eax
    jne .Lsp_fail
    movl $0x474e4950, 94000(%r12)   // PING
    movl $94000, 90272(%r12)
    movl $4, 90276(%r12)
    SOCK_CALL 3, 90256
    cmpl $4, %eax
    jne .Lsp_fail
    movl $94400, 90400(%r12)
    movl $4, 90404(%r12)
    SOCK_CALL 4, 90384
    cmpl $4, %eax
    jne .Lsp_fail
    cmpl $0x474e4950, 94400(%r12)
    jne .Lsp_fail

    // The initial accept consumed the one pending connection.
    // With no next client, a second accept must time out.
    movl -32(%rbp), %eax
    movl %eax, 90128(%r12)
    movl $50, 90140(%r12)
    SOCK_CALL 2, 90128
    cmpl $-110, %eax
    jne .Lsp_fail
    cmpl $110, 90156(%r12)
    jne .Lsp_fail

    // UDP receiver and sender both use loopback ephemeral ports.
    movl $0, 90520(%r12)
    movl $1000, 90524(%r12)
    movl $0x0100007f, 90544(%r12)
    SOCK_CALL 7, 90512
    testl %eax, %eax
    js .Lsp_fail
    movl %eax, -44(%rbp)
    SOCK_CALL 10, 90512
    cmpl $1, %eax
    jl .Lsp_fail
    movl $0, 90648(%r12)
    movl $1000, 90652(%r12)
    movl $0x0100007f, 90672(%r12)
    SOCK_CALL 7, 90640
    testl %eax, %eax
    js .Lsp_fail
    movl %eax, -48(%rbp)

    // After getsockname, receiver's sockaddr has its allocated port.
    movq 90560(%r12), %rax
    movq %rax, 90688(%r12)
    movq 90568(%r12), %rax
    movq %rax, 90696(%r12)
    movl $16, 90752(%r12)
    movl $94000, 90656(%r12)
    movl $4, 90660(%r12)
    SOCK_CALL 9, 90640
    cmpl $4, %eax
    jne .Lsp_fail
    movl $94800, 90528(%r12)
    movl $4, 90532(%r12)
    SOCK_CALL 8, 90512
    cmpl $4, %eax
    jne .Lsp_fail
    cmpl $0x474e4950, 94800(%r12)
    jne .Lsp_fail

    // Bad input with a valid control block reports EINVAL in the block.
    movl $-1, 90768(%r12)
    movl $94000, 90784(%r12)
    movl $0, 90788(%r12)
    SOCK_CALL 4, 90768
    cmpl $-22, %eax
    jne .Lsp_fail
    cmpl $22, 90796(%r12)
    jne .Lsp_fail
    // A four-byte buffer at the very end of the arena is valid. The
    // deliberately invalid descriptor must yield EBADF, not EINVAL.
    movl $999999, 90768(%r12)
    movl $(PROBE_ARENA_SIZE - 4), 90784(%r12)
    movl $4, 90788(%r12)
    SOCK_CALL 4, 90768
    cmpl $-9, %eax
    jne .Lsp_fail
    cmpl $9, 90796(%r12)
    jne .Lsp_fail

    // Close via the actual V++ socket ABI, including fd invalidation.
    SOCK_CALL 5, 90384
    testl %eax, %eax
    jne .Lsp_fail
    movl $-1, -40(%rbp)
    SOCK_CALL 5, 90256
    testl %eax, %eax
    jne .Lsp_fail
    movl $-1, -36(%rbp)
    SOCK_CALL 5, 90000
    testl %eax, %eax
    jne .Lsp_fail
    movl $-1, -32(%rbp)
    SOCK_CALL 5, 90512
    testl %eax, %eax
    jne .Lsp_fail
    movl $-1, -44(%rbp)
    SOCK_CALL 5, 90640
    testl %eax, %eax
    jne .Lsp_fail
    movl $-1, -48(%rbp)
    xorl %eax, %eax
    jmp .Lsp_cleanup
.Lsp_fail:
    movl $1, %eax
.Lsp_cleanup:
    movl %eax, -52(%rbp)
    leaq -32(%rbp), %r13
    movl $5, %ebx
.Lsp_cleanup_loop:
    movl (%r13), %edi
    testl %edi, %edi
    js .Lsp_cleanup_next
    call vpp_sys_close
.Lsp_cleanup_next:
    subq $4, %r13
    decl %ebx
    jnz .Lsp_cleanup_loop
    movl -52(%rbp), %eax
    addq $72, %rsp
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.size vpp_freestanding_socket_probe, .-vpp_freestanding_socket_probe
.section .note.GNU-stack,"",@progbits
