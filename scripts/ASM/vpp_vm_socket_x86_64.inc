// POSIX x86_64 socket boundary. All pointers are derived from checked arena
// offsets. Tokens, type checks and ownership belong to V++.
// control: fd@0, kind@4, port@8, timeout_ms@12, bytes@16, count@20,
// ip_count@24, OS errno@28, ip@32..47, sockaddr@48..79, timeval@80..95,
// pollfd@96, saved flags@104, connection error@108, sockaddr length@112.
// Operations 7/8/9/10 add UDP bind/recvfrom/sendto/local port for V++ mocks.
// Monotonic deadline is kept on the private ASM stack.
#ifdef __APPLE__
#define SOCK_SYM(x) _##x
#define SOCK_LIB(x) _##x
#define SOCK_LEVEL 0xffff
#define SOCK_REUSE 4
#define SOCK_RCVTIMEO 0x1006
#define SOCK_SNDTIMEO 0x1005
#define SOCK_NOSIGPIPE 0x1022
#define SOCK_MSGFLAGS 0
#define SOCK_INET6 30
#define SOCK_NONBLOCK 4
#define SOCK_INPROGRESS 36
#define SOCK_TIMEDOUT 60
#define SOCK_SOERROR 0x1007
#define SOCK_CLOCK 6
#else
#define SOCK_SYM(x) x
#define SOCK_LIB(x) x@PLT
#define SOCK_LEVEL 1
#define SOCK_REUSE 2
#define SOCK_RCVTIMEO 20
#define SOCK_SNDTIMEO 21
#define SOCK_MSGFLAGS 0x4000
#define SOCK_INET6 10
#define SOCK_NONBLOCK 2048
#define SOCK_INPROGRESS 115
#define SOCK_TIMEDOUT 110
#define SOCK_SOERROR 4
#define SOCK_CLOCK 1
#endif
.text
.globl SOCK_SYM(vpp_native_os_socket)
SOCK_SYM(vpp_native_os_socket):
    pushq %rbp
    movq %rsp, %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    subq $136, %rsp
    movl %edi, %ebx                  // operation
    movl %esi, %r13d                 // control offset
    call SOCK_SYM(vpp_native_memory_base)
    testq %rax, %rax
    je .Lsock_invalid_x
    cmpl $10, %ebx
    ja .Lsock_invalid_x
    cmpl $(68162560-128), %r13d
    ja .Lsock_invalid_x
    testl $15, %r13d
    jnz .Lsock_invalid_x
    leaq (%rax,%r13), %r12
    movl $0, 28(%r12)
    cmpl $7, %ebx
    je .Lsock_udp_bind_x
    cmpl $8, %ebx
    je .Lsock_io_x
    cmpl $9, %ebx
    je .Lsock_io_x
    cmpl $10, %ebx
    je .Lsock_port_x
    cmpl $0, %ebx
    je .Lsock_connect_x
    cmpl $1, %ebx
    je .Lsock_listen_x
    cmpl $2, %ebx
    je .Lsock_accept_x
    cmpl $3, %ebx
    je .Lsock_io_x
    cmpl $4, %ebx
    je .Lsock_io_x
    cmpl $5, %ebx
    je .Lsock_close_x
    jmp .Lsock_timeout_x
.Lsock_address_x:
    // input: socket address family in edi, port in control, ip at +32.
    movl %edi, %edx
    leaq 48(%r12), %rdi
    xorl %eax, %eax
    movl $32, %ecx
    rep stosb
#ifdef __APPLE__
    cmpl $2, %edx
    jne 1f
    movb $16, 48(%r12)
    jmp 2f
1:  movb $28, 48(%r12)
2:  movb %dl, 49(%r12)
#else
    movw %dx, 48(%r12)
#endif
    movl 8(%r12), %eax
    rolw $8, %ax
    movw %ax, 50(%r12)
    cmpl $2, %edx
    jne 3f
    movl 32(%r12), %eax
    movl %eax, 52(%r12)
    ret
3:  movq 32(%r12), %rax
    movq %rax, 56(%r12)
    movq 40(%r12), %rax
    movq %rax, 64(%r12)
    ret
.Lsock_udp_bind_x:
    movl 8(%r12), %eax
    testl %eax, %eax
    js .Lsock_invalid_x
    cmpl $65535, %eax
    jg .Lsock_invalid_x
    movl 12(%r12), %eax
    testl %eax, %eax
    jle .Lsock_invalid_x
    cmpl $300000, %eax
    jg .Lsock_invalid_x
    movl $2, %edi
    call .Lsock_address_x
    movl $2, %edi
    movl $2, %esi
    xorl %edx, %edx
    call SOCK_LIB(socket)
    testl %eax, %eax
    js .Lsock_error_return_x
    movl %eax, (%r12)
    call .Lsock_set_timeout_x
    testl %eax, %eax
    js .Lsock_close_error_x
    movl (%r12), %edi
    leaq 48(%r12), %rsi
    movl $16, %edx
    call SOCK_LIB(bind)
    testl %eax, %eax
    js .Lsock_errno_close_x
    movl (%r12), %eax
    jmp .Lsock_return_x
.Lsock_port_x:
    movl (%r12), %edi
    testl %edi, %edi
    js .Lsock_invalid_x
    movl $32, 112(%r12)
    leaq 48(%r12), %rsi
    leaq 112(%r12), %rdx
    call SOCK_LIB(getsockname)
    testl %eax, %eax
    js .Lsock_error_return_x
    movzwl 50(%r12), %eax
    rolw $8, %ax
    movl %eax, 8(%r12)
    jmp .Lsock_return_x
.Lsock_connect_x:
    movl 24(%r12), %eax
    cmpl $4, %eax
    je 1f
    cmpl $16, %eax
    jne .Lsock_invalid_x
1:  movl 8(%r12), %eax
    testl %eax, %eax
    jle .Lsock_invalid_x
    cmpl $65535, %eax
    jg .Lsock_invalid_x
    cmpl $1, 4(%r12)
    ja .Lsock_invalid_x
    movl $2, %edi
    cmpl $16, 24(%r12)
    jne 2f
    movl $SOCK_INET6, %edi
2:  movl %edi, %ebx
    call .Lsock_address_x
    movl %ebx, %edi
    movl $1, %esi
    cmpl $1, 4(%r12)
    jne 3f
    movl $2, %esi
3:  xorl %edx, %edx
    call SOCK_LIB(socket)
    testl %eax, %eax
    js .Lsock_error_return_x
    movl %eax, 0(%r12)
    call .Lsock_nosigpipe_x
    testl %eax, %eax
    js .Lsock_close_error_x
    // Set SO_SNDTIMEO/SO_RCVTIMEO before connect to bound blocking I/O.
    movl 12(%r12), %eax
    testl %eax, %eax
    jle .Lsock_invalid_fd_x
    cmpl $300000, %eax
    jg .Lsock_invalid_fd_x
    call .Lsock_set_timeout_x
    testl %eax, %eax
    js .Lsock_close_error_x
    // SO_SNDTIMEO does not limit connect(). Switch to nonblocking and
    // restore the socket flags before returning its descriptor.
    movl (%r12), %edi
    movl $3, %esi                 // F_GETFL
    xorl %eax, %eax
    call SOCK_LIB(fcntl)
    testl %eax, %eax
    js .Lsock_errno_close_x
    movl %eax, 104(%r12)
    orl $SOCK_NONBLOCK, %eax
    movl %eax, %edx
    movl (%r12), %edi
    movl $4, %esi                 // F_SETFL
    xorl %eax, %eax
    call SOCK_LIB(fcntl)
    testl %eax, %eax
    js .Lsock_errno_close_x
    movl 0(%r12), %edi
    leaq 48(%r12), %rsi
    movl $16, %edx
    cmpl $16, 24(%r12)
    jne 4f
    movl $28, %edx
4:  call SOCK_LIB(connect)
    testl %eax, %eax
    jns .Lsock_connected_x
    call .Lsock_errno_x
    cmpl $-SOCK_INPROGRESS, %eax
    je .Lsock_connect_pending_x
    cmpl $-4, %eax               // EINTR from initial connect
    jne .Lsock_connect_restore_error_x
.Lsock_connect_pending_x:
    movl (%r12), %eax
    movl %eax, 96(%r12)
    movw $4, 100(%r12)            // POLLOUT
    movw $0, 102(%r12)
    call .Lsock_deadline_start_x
    testl %eax, %eax
    js .Lsock_connect_restore_error_x
.Lsock_connect_poll_loop_x:
    call .Lsock_deadline_remaining_x
    testl %eax, %eax
    js .Lsock_connect_restore_error_x
    jz .Lsock_connect_timedout_x
    movl %eax, %edx
    leaq 96(%r12), %rdi
    movl $1, %esi
    call SOCK_LIB(poll)
    testl %eax, %eax
    js .Lsock_connect_poll_error_x
    jz .Lsock_connect_timedout_x
    movl (%r12), %edi
    movl $SOCK_LEVEL, %esi
    movl $SOCK_SOERROR, %edx
    leaq 108(%r12), %rcx
    movl $4, 112(%r12)
    leaq 112(%r12), %r8
    call SOCK_LIB(getsockopt)
    testl %eax, %eax
    js .Lsock_connect_poll_error_x
    movl 108(%r12), %eax
    testl %eax, %eax
    jz .Lsock_connected_x
    movl %eax, 28(%r12)
    negl %eax
    jmp .Lsock_connect_restore_error_x
.Lsock_connect_timedout_x:
    movl $SOCK_TIMEDOUT, 28(%r12)
    movl $-SOCK_TIMEDOUT, %eax
    jmp .Lsock_connect_restore_error_x
.Lsock_connect_poll_error_x:
    call .Lsock_errno_x
    cmpl $-4, %eax                // EINTR
    je .Lsock_connect_poll_loop_x
.Lsock_connect_restore_error_x:
    movl %eax, %ebx
    movl (%r12), %edi
    movl $4, %esi
    movl 104(%r12), %edx
    xorl %eax, %eax
    call SOCK_LIB(fcntl)
    movl %ebx, %eax
    jmp .Lsock_close_error_x
.Lsock_connected_x:
    movl (%r12), %edi
    movl $4, %esi
    movl 104(%r12), %edx
    xorl %eax, %eax
    call SOCK_LIB(fcntl)
    testl %eax, %eax
    js .Lsock_errno_close_x
    movl 0(%r12), %eax
    jmp .Lsock_return_x
.Lsock_listen_x:
    movl 8(%r12), %eax
    testl %eax, %eax
    jle .Lsock_invalid_x
    cmpl $65535, %eax
    jg .Lsock_invalid_x
    movl 20(%r12), %eax
    testl %eax, %eax
    jle .Lsock_invalid_x
    cmpl $1024, %eax
    jg .Lsock_invalid_x
    movl $2, %edi
    call .Lsock_address_x
    movl $2, %edi
    movl $1, %esi
    xorl %edx, %edx
    call SOCK_LIB(socket)
    testl %eax, %eax
    js .Lsock_error_return_x
    movl %eax, (%r12)
    movl $1, 96(%r12)
    movl (%r12), %edi
    movl $SOCK_LEVEL, %esi
    movl $SOCK_REUSE, %edx           // SO_REUSEADDR
    leaq 96(%r12), %rcx
    movl $4, %r8d
    call SOCK_LIB(setsockopt)
    testl %eax, %eax
    js .Lsock_errno_close_x
    movl (%r12), %edi
    leaq 48(%r12), %rsi
    movl $16, %edx
    call SOCK_LIB(bind)
    testl %eax, %eax
    js .Lsock_errno_close_x
    movl (%r12), %edi
    movl 20(%r12), %esi
    call SOCK_LIB(listen)
    testl %eax, %eax
    js .Lsock_errno_close_x
    movl (%r12), %eax
    jmp .Lsock_return_x
.Lsock_accept_x:
    // An explicit poll deadline bounds accept() on both POSIX ABIs.
    movl (%r12), %eax
    testl %eax, %eax
    js .Lsock_invalid_x
    movl %eax, 96(%r12)
    movw $1, 100(%r12)            // POLLIN
    movw $0, 102(%r12)
    movl 12(%r12), %edx
    testl %edx, %edx
    jle .Lsock_invalid_x
    call .Lsock_deadline_start_x
    testl %eax, %eax
    js .Lsock_return_x
.Lsock_accept_poll_loop_x:
    call .Lsock_deadline_remaining_x
    testl %eax, %eax
    js .Lsock_return_x
    jz .Lsock_accept_timedout_x
    movl %eax, %edx
    leaq 96(%r12), %rdi
    movl $1, %esi
    call SOCK_LIB(poll)
    testl %eax, %eax
    js .Lsock_accept_poll_error_x
    jz .Lsock_accept_timedout_x
    movl (%r12), %edi
    xorl %esi, %esi
    xorl %edx, %edx
    call SOCK_LIB(accept)
    testl %eax, %eax
    js .Lsock_error_return_x
    movl %eax, (%r12)
    call .Lsock_nosigpipe_x
    testl %eax, %eax
    js .Lsock_close_error_x
    call .Lsock_set_timeout_x
    testl %eax, %eax
    js .Lsock_close_error_x
    movl (%r12), %eax
    jmp .Lsock_return_x
.Lsock_accept_timedout_x:
    movl $SOCK_TIMEDOUT, 28(%r12)
    movl $-SOCK_TIMEDOUT, %eax
    jmp .Lsock_return_x
.Lsock_accept_poll_error_x:
    call .Lsock_errno_x
    cmpl $-4, %eax               // EINTR
    je .Lsock_accept_poll_loop_x
    jmp .Lsock_return_x
.Lsock_io_x:
    movl 16(%r12), %esi
    movl 20(%r12), %edx
    testl %edx, %edx
    jle .Lsock_invalid_x
    cmpl $65536, %edx
    jg .Lsock_invalid_x
    cmpl $(68162560-65536), %esi
    ja .Lsock_invalid_x
    movl %esi, %eax
    addq %rax, %rdx
    cmpq $68162560, %rdx
    ja .Lsock_invalid_x
    movl %esi, %r13d             // libc may clobber caller-saved rsi
    call SOCK_SYM(vpp_native_memory_base)
    leaq (%rax,%r13), %rsi
    movl (%r12), %edi
    movl 20(%r12), %edx
    movl $SOCK_MSGFLAGS, %ecx
    cmpl $8, %ebx
    je .Lsock_recvfrom_x
    cmpl $9, %ebx
    je .Lsock_sendto_x
    cmpl $3, %ebx
    jne 1f
    call SOCK_LIB(send)
    jmp 2f
1:  call SOCK_LIB(recv)
2:  testl %eax, %eax
    js .Lsock_error_return_x
    jmp .Lsock_return_x
.Lsock_recvfrom_x:
    xorl %ecx, %ecx
    movl $32, 112(%r12)
    leaq 48(%r12), %r8
    leaq 112(%r12), %r9
    call SOCK_LIB(recvfrom)
    testl %eax, %eax
    js .Lsock_error_return_x
    jmp .Lsock_return_x
.Lsock_sendto_x:
    cmpl $16, 112(%r12)
    jne .Lsock_invalid_x
    leaq 48(%r12), %r8
    movl $16, %r9d
    call SOCK_LIB(sendto)
    testl %eax, %eax
    js .Lsock_error_return_x
    jmp .Lsock_return_x
.Lsock_close_x:
    movl (%r12), %edi
    testl %edi, %edi
    js .Lsock_invalid_x
    call SOCK_LIB(close)
    testl %eax, %eax
    js .Lsock_errno_x
    movl $-1, (%r12)
    xorl %eax, %eax
    jmp .Lsock_return_x
.Lsock_timeout_x:
    movl (%r12), %edi
    testl %edi, %edi
    js .Lsock_invalid_x
    movl 12(%r12), %eax
    testl %eax, %eax
    jle .Lsock_invalid_x
    cmpl $300000, %eax
    jg .Lsock_invalid_x
    call .Lsock_set_timeout_x
    jmp .Lsock_return_x
.Lsock_set_timeout_x:
    // Called with live control in r12; returns 0 or negative errno.
    // Helpers enter with rsp%16==8. Align before calling libc (SysV ABI).
    subq $8, %rsp
    movl 12(%r12), %eax
    xorl %edx, %edx
    movl $1000, %ecx
    divl %ecx
    movq %rax, 80(%r12)
    imull $1000, %edx, %edx
    movq %rdx, 88(%r12)
    movl (%r12), %edi
    movl $SOCK_LEVEL, %esi
    movl $SOCK_RCVTIMEO, %edx
    leaq 80(%r12), %rcx
    movl $16, %r8d
    call SOCK_LIB(setsockopt)
    testl %eax, %eax
    js .Lsock_timeout_error_x
    movl (%r12), %edi
    movl $SOCK_LEVEL, %esi
    movl $SOCK_SNDTIMEO, %edx
    leaq 80(%r12), %rcx
    movl $16, %r8d
    call SOCK_LIB(setsockopt)
    testl %eax, %eax
    js .Lsock_timeout_error_x
    xorl %eax, %eax
    addq $8, %rsp
    ret
.Lsock_timeout_error_x:
    call .Lsock_errno_x
    addq $8, %rsp
    ret
.Lsock_nosigpipe_x:
    subq $8, %rsp
#ifdef __APPLE__
    movl $1, 96(%r12)
    movl (%r12), %edi
    movl $SOCK_LEVEL, %esi
    movl $SOCK_NOSIGPIPE, %edx
    leaq 96(%r12), %rcx
    movl $4, %r8d
    call SOCK_LIB(setsockopt)
    testl %eax, %eax
    js .Lsock_nosigpipe_error_x
#endif
    xorl %eax, %eax
    addq $8, %rsp
    ret
.Lsock_nosigpipe_error_x:
    call .Lsock_errno_x
    addq $8, %rsp
    ret
// Shared helpers reserve caller frame slots: timespec @48..63,
// absolute deadline ms @80, while aligning their own libc calls.
.Lsock_deadline_start_x:
    subq $8, %rsp
    movl $SOCK_CLOCK, %edi
    leaq 64(%rsp), %rsi
    call SOCK_LIB(clock_gettime)
    testl %eax, %eax
    js .Lsock_deadline_errno_x
    call .Lsock_now_ms_x
    movl 12(%r12), %ecx
    addq %rcx, %rax
    movq %rax, 96(%rsp)
    xorl %eax, %eax
    jmp .Lsock_deadline_ret_x
.Lsock_deadline_remaining_x:
    subq $8, %rsp
    movl $SOCK_CLOCK, %edi
    leaq 64(%rsp), %rsi
    call SOCK_LIB(clock_gettime)
    testl %eax, %eax
    js .Lsock_deadline_errno_x
    call .Lsock_now_ms_x
    movq 96(%rsp), %rcx
    subq %rax, %rcx
    jle .Lsock_deadline_expired_x
    movl %ecx, %eax
    jmp .Lsock_deadline_ret_x
.Lsock_deadline_expired_x:
    xorl %eax, %eax
    jmp .Lsock_deadline_ret_x
.Lsock_deadline_errno_x:
    call .Lsock_errno_x
.Lsock_deadline_ret_x:
    addq $8, %rsp
    ret
.Lsock_now_ms_x:
    // call pushed its return address: helper's time fields moved by +8.
    movq 80(%rsp), %rax
    xorl %edx, %edx
    movq $1000000, %rcx
    divq %rcx
    movq 72(%rsp), %rcx
    imulq $1000, %rcx, %rcx
    addq %rcx, %rax
    ret
.Lsock_close_error_x:
    movl %eax, %ebx
    movl (%r12), %edi
    call SOCK_LIB(close)
    movl $-1, (%r12)
    movl %ebx, %eax
    jmp .Lsock_return_x
.Lsock_errno_close_x:
    call .Lsock_errno_x
    jmp .Lsock_close_error_x
.Lsock_error_return_x:
    call .Lsock_errno_x
    jmp .Lsock_return_x
.Lsock_invalid_fd_x:
    movl $-22, %eax
    jmp .Lsock_close_error_x
.Lsock_errno_x:
    // Called from aligned outer entry and from aligned nested helpers.
    subq $8, %rsp
#ifdef __APPLE__
    call SOCK_LIB(__error)
#else
    call SOCK_LIB(__errno_location)
#endif
    addq $8, %rsp
    movl (%rax), %eax
    movl %eax, 28(%r12)
    negl %eax
    ret
.Lsock_invalid_x:
    movl $-22, %eax
.Lsock_return_x:
    addq $136, %rsp
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
