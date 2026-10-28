// x86-64 SysV implementation of the shared V++ process control block.
// All guest addresses are checked arena offsets, never supplied host pointers.
// rbx=arena, r12=control, r13d=operation; stack remains aligned for libc.
.macro PX_RETURN
    leaq 8(%rsp), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.endm
.macro PX_ERRNO
#ifdef __APPLE__
    call VM_PROC_LIB(__error)
#else
    call VM_PROC_LIB(__errno_location)
#endif
    movl (%rax), %eax
.endm

.globl VM_PROC_SYM(vpp_native_os_process)
VM_PROC_SYM(vpp_native_os_process):
    pushq %rbp
    movq %rsp, %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $8, %rsp
    movl %edi, %r13d
    movl %esi, %r14d
    call VM_PROC_SYM(vpp_native_memory_base)
    movq %rax, %rbx
    testq %rbx, %rbx
    je Lpx_invalid
    cmpl $4, %r13d
    ja Lpx_invalid
    cmpl $(67000000-1024), %r14d
    ja Lpx_invalid
    testl $15, %r14d
    jnz Lpx_invalid
    leaq (%rbx,%r14), %r12
    testl %r13d, %r13d
    je Lpx_spawn
    cmpl $1, %r13d
    je Lpx_poll
    cmpl $2, %r13d
    je Lpx_read
    cmpl $3, %r13d
    je Lpx_wait
    jmp Lpx_close

Lpx_spawn:
    movl 20(%r12), %r15d
    cmpl $1, %r15d
    jl Lpx_invalid
    cmpl $128, %r15d
    jg Lpx_invalid
    movl 12(%r12), %eax
    testl $3, %eax
    jnz Lpx_invalid
    cmpl $(67000000-512), %eax
    ja Lpx_invalid
    movl 16(%r12), %ecx
    testl $7, %ecx
    jnz Lpx_invalid
    cmpl $(67000000-1032), %ecx
    ja Lpx_invalid
    xorl %r14d, %r14d
Lpx_argv:
    cmpl %r15d, %r14d
    jge Lpx_argv_done
    movl 12(%r12), %eax
    leaq (%rbx,%rax), %r10
    movl (%r10,%r14,4), %ecx
    cmpl $8388624, %ecx
    jb Lpx_invalid
    cmpl $(67000000-4096), %ecx
    ja Lpx_invalid
    leaq (%rbx,%rcx), %rdx
    movl 16(%r12), %eax
    leaq (%rbx,%rax), %r8
    movq %rdx, (%r8,%r14,8)
    xorl %r9d, %r9d
Lpx_arg_byte:
    cmpb $0, (%rdx,%r9)
    je Lpx_arg_ok
    incl %r9d
    cmpl $4096, %r9d
    jb Lpx_arg_byte
    jmp Lpx_invalid
Lpx_arg_ok:
    incl %r14d
    jmp Lpx_argv
Lpx_argv_done:
    movq $0, (%r8,%r14,8)
    movl $0, 48(%r12)
    movl $-1, 64(%r12)
    movl $-1, 68(%r12)
    movl $-1, 72(%r12)
    movl $-1, 76(%r12)
    movl $0, 88(%r12)
    movl $0, 92(%r12)
    movl $0, 96(%r12)
    leaq 64(%r12), %rdi
    call VM_PROC_LIB(pipe)
    testl %eax, %eax
    jnz Lpx_spawn_errno
    leaq 72(%r12), %rdi
    call VM_PROC_LIB(pipe)
    testl %eax, %eax
    jnz Lpx_spawn_errno
    xorl %r14d, %r14d
Lpx_fd_promote:
    cmpl $4, %r14d
    jge Lpx_fd_cloexec_init
    movl 64(%r12,%r14,4), %edi
    cmpl $3, %edi
    jge Lpx_fd_promote_next
#ifdef __APPLE__
    movl $67, %esi                 // F_DUPFD_CLOEXEC
#else
    movl $1030, %esi
#endif
    movl $3, %edx
    call VM_PROC_LIB(fcntl)
    testl %eax, %eax
    js Lpx_spawn_errno
    movl %eax, %r15d
    movl 64(%r12,%r14,4), %edi
    call VM_PROC_LIB(close)
    movl %r15d, 64(%r12,%r14,4)
Lpx_fd_promote_next:
    incl %r14d
    jmp Lpx_fd_promote
Lpx_fd_cloexec_init:
    xorl %r14d, %r14d
Lpx_fd_cloexec:
    movl 64(%r12,%r14,4), %edi
    movl $2, %esi
    movl $1, %edx
    call VM_PROC_LIB(fcntl)
    testl %eax, %eax
    js Lpx_spawn_errno
    incl %r14d
    cmpl $4, %r14d
    jl Lpx_fd_cloexec
    leaq 192(%r12), %rdi
    call VM_PROC_LIB(posix_spawn_file_actions_init)
    testl %eax, %eax
    jnz Lpx_spawn_error
    movl $1, 88(%r12)
    leaq 192(%r12), %rdi
    movl 68(%r12), %esi
    movl $1, %edx
    call VM_PROC_LIB(posix_spawn_file_actions_adddup2)
    testl %eax, %eax
    jnz Lpx_spawn_error
    leaq 192(%r12), %rdi
    movl 76(%r12), %esi
    movl $2, %edx
    call VM_PROC_LIB(posix_spawn_file_actions_adddup2)
    testl %eax, %eax
    jnz Lpx_spawn_error
    xorl %r14d, %r14d
Lpx_fd_addclose:
    leaq 192(%r12), %rdi
    movl 64(%r12,%r14,4), %esi
    call VM_PROC_LIB(posix_spawn_file_actions_addclose)
    testl %eax, %eax
    jnz Lpx_spawn_error
    incl %r14d
    cmpl $4, %r14d
    jl Lpx_fd_addclose
    leaq 320(%r12), %rdi
    call VM_PROC_LIB(posix_spawnattr_init)
    testl %eax, %eax
    jnz Lpx_spawn_error
    movl $1, 92(%r12)
    leaq 320(%r12), %rdi
    movl $2, %esi                 // POSIX_SPAWN_SETPGROUP
    call VM_PROC_LIB(posix_spawnattr_setflags)
    testl %eax, %eax
    jnz Lpx_spawn_error
    leaq 320(%r12), %rdi
    xorl %esi, %esi
    call VM_PROC_LIB(posix_spawnattr_setpgroup)
    testl %eax, %eax
    jnz Lpx_spawn_error

#ifdef __APPLE__
    call VM_PROC_LIB(_NSGetEnviron)
    movq (%rax), %r9
#else
    movq environ@GOTPCREL(%rip), %rax
    movq (%rax), %r9
#endif
    movq %r9, 104(%r12)
    movl 44(%r12), %eax
    testl %eax, %eax
    je Lpx_env_ready
    cmpl $128, %eax
    ja Lpx_bad_env
    movl 24(%r12), %ecx
    testl $3, %ecx
    jnz Lpx_bad_env
    cmpl $(67000000-512), %ecx
    ja Lpx_bad_env
    leaq (%rbx,%rcx), %r10
    movq %r10, 120(%r12)
    movl 56(%r12), %edx
    testl $7, %edx
    jnz Lpx_bad_env
    cmpl $(67000000-33824), %edx
    ja Lpx_bad_env
    leaq (%rbx,%rdx), %r10
    movq %r10, 112(%r12)
    movl $0, 128(%r12)
    movl $0, 132(%r12)
Lpx_env_inherited:
    cmpl $4096, 128(%r12)
    jae Lpx_bad_env
    movq 104(%r12), %r10
    movl 128(%r12), %eax
    movq (%r10,%rax,8), %rdx
    testq %rdx, %rdx
    je Lpx_env_append
    movl $0, 136(%r12)
Lpx_env_check:
    movl 136(%r12), %eax
    cmpl 44(%r12), %eax
    jae Lpx_env_keep
    movq 120(%r12), %r10
    movl (%r10,%rax,4), %ecx
    cmpl $8388624, %ecx
    jb Lpx_bad_env
    cmpl $(67000000-4096), %ecx
    ja Lpx_bad_env
    leaq (%rbx,%rcx), %r8
    xorl %r9d, %r9d
Lpx_env_compare:
    movzbl (%r8,%r9), %eax
    cmpl $61, %eax
    je Lpx_env_key_end
    cmpb (%rdx,%r9), %al
    jne Lpx_env_next_override
    incl %r9d
    cmpl $4096, %r9d
    jb Lpx_env_compare
    jmp Lpx_bad_env
Lpx_env_key_end:
    cmpb $61, (%rdx,%r9)
    je Lpx_env_skip
Lpx_env_next_override:
    incl 136(%r12)
    jmp Lpx_env_check
Lpx_env_keep:
    movq 112(%r12), %r10
    movl 132(%r12), %eax
    movq %rdx, (%r10,%rax,8)
    incl 132(%r12)
Lpx_env_skip:
    incl 128(%r12)
    jmp Lpx_env_inherited
Lpx_env_append:
    xorl %r14d, %r14d
Lpx_env_add:
    cmpl 44(%r12), %r14d
    jge Lpx_env_terminate
    movq 120(%r12), %r10
    movl (%r10,%r14,4), %ecx
    cmpl $8388624, %ecx
    jb Lpx_bad_env
    cmpl $(67000000-4096), %ecx
    ja Lpx_bad_env
    leaq (%rbx,%rcx), %rdx
    movq 112(%r12), %r10
    movl 132(%r12), %eax
    movq %rdx, (%r10,%rax,8)
    incl 132(%r12)
    incl %r14d
    jmp Lpx_env_add
Lpx_env_terminate:
    movq 112(%r12), %r10
    movl 132(%r12), %eax
    movq $0, (%r10,%rax,8)
    movq %r10, 104(%r12)
Lpx_env_ready:
    movl 16(%r12), %eax
    leaq (%rbx,%rax), %r8
    movq (%r8), %rsi
    movq %r12, %rdi
    leaq 192(%r12), %rdx
    leaq 320(%r12), %rcx
    movq 104(%r12), %r9
    call VM_PROC_LIB(posix_spawnp)
    testl %eax, %eax
    jnz Lpx_spawn_error
    movl $1, 96(%r12)
    jmp Lpx_spawn_finalize
Lpx_bad_env:
    movl $22, %eax
    jmp Lpx_spawn_error
Lpx_spawn_errno:
    PX_ERRNO
Lpx_spawn_error:
    movl %eax, 48(%r12)
    movl $0, 96(%r12)
Lpx_spawn_finalize:
    cmpl $0, 92(%r12)
    je 1f
    leaq 320(%r12), %rdi
    call VM_PROC_LIB(posix_spawnattr_destroy)
1:
    cmpl $0, 88(%r12)
    je 2f
    leaq 192(%r12), %rdi
    call VM_PROC_LIB(posix_spawn_file_actions_destroy)
2:
    movl 68(%r12), %edi
    cmpl $0, %edi
    jl 3f
    call VM_PROC_LIB(close)
3:
    movl 76(%r12), %edi
    cmpl $0, %edi
    jl 4f
    call VM_PROC_LIB(close)
4:
    movl $-1, 68(%r12)
    movl $-1, 76(%r12)
    cmpl $0, 96(%r12)
    je Lpx_spawn_failed
    movl 64(%r12), %eax
    movl %eax, 4(%r12)
    movl 72(%r12), %eax
    movl %eax, 8(%r12)
    movl $1, %eax
    PX_RETURN
Lpx_spawn_failed:
    movl 64(%r12), %edi
    cmpl $0, %edi
    jl 5f
    call VM_PROC_LIB(close)
5:
    movl 72(%r12), %edi
    cmpl $0, %edi
    jl 6f
    call VM_PROC_LIB(close)
6:
    xorl %eax, %eax
    PX_RETURN

Lpx_poll:
    xorl %r15d, %r15d
    movl 4(%r12), %eax
    testl %eax, %eax
    js 1f
    movl %eax, 64(%r12)
    movw $1, 68(%r12)
    movw $0, 70(%r12)
    incl %r15d
1:
    movl 8(%r12), %eax
    testl %eax, %eax
    js 2f
    movl %eax, 64(%r12,%r15,8)
    movw $1, 68(%r12,%r15,8)
    movw $0, 70(%r12,%r15,8)
    incl %r15d
2:
    testl %r15d, %r15d
    je Lpx_poll_eof
    movl 28(%r12), %edx
    cmpl $5000, %edx
    ja Lpx_invalid
    movq %r12, %rdi
    addq $64, %rdi
    movl %r15d, %esi
    call VM_PROC_LIB(poll)
    testl %eax, %eax
    js Lpx_io_errno
    xorl %eax, %eax
    movl 4(%r12), %ecx
    testl %ecx, %ecx
    js 3f
    testw $0x39, 70(%r12)
    setnz %al
3:
    movl 8(%r12), %ecx
    testl %ecx, %ecx
    js 4f
    movl 4(%r12), %ecx
    testl %ecx, %ecx
    jns 5f
    testw $0x39, 70(%r12)
    jmp 6f
5:
    testw $0x39, 78(%r12)
6:
    setnz %cl
    movzbl %cl, %ecx
    leal (%rax,%rcx,2), %eax
4:
    PX_RETURN
Lpx_poll_eof:
    movl $4, %eax
    PX_RETURN

Lpx_read:
    movl 40(%r12), %r14d
    cmpl $1, %r14d
    je 1f
    cmpl $2, %r14d
    jne Lpx_invalid
1:
    movl 36(%r12), %r15d
    cmpl $1, %r15d
    jl Lpx_invalid
    cmpl $4096, %r15d
    jg Lpx_invalid
    movl 32(%r12), %eax
    cmpl $(67000000-4096), %eax
    ja Lpx_invalid
    leal (%rax,%r15), %ecx
    cmpl $67000000, %ecx
    ja Lpx_invalid
    leaq (%rbx,%rax), %r15
    leaq (%r12,%r14,4), %r14
    movl (%r14), %edi
    testl %edi, %edi
    js Lpx_read_eof
Lpx_read_retry:
    movq %r15, %rsi
    movl 36(%r12), %edx
    call VM_PROC_LIB(read)
    testq %rax, %rax
    jg Lpx_return
    je Lpx_read_closed
    PX_ERRNO
    cmpl $4, %eax
    je Lpx_read_again
    negl %eax
    PX_RETURN
Lpx_read_again:
    movl (%r14), %edi
    jmp Lpx_read_retry
Lpx_read_closed:
    movl (%r14), %edi
    call VM_PROC_LIB(close)
    movl $-1, (%r14)
Lpx_read_eof:
    xorl %eax, %eax
    PX_RETURN

Lpx_wait:
    movl (%r12), %r14d
    testl %r14d, %r14d
    jle Lpx_invalid
    cmpl $0, 4(%r12)
    jge Lpx_invalid
    cmpl $0, 8(%r12)
    jge Lpx_invalid
Lpx_wait_retry:
    movl %r14d, %edi
    leaq 100(%r12), %rsi
    xorl %edx, %edx
    call VM_PROC_LIB(waitpid)
    testl %eax, %eax
    jg Lpx_wait_done
    PX_ERRNO
    cmpl $4, %eax
    je Lpx_wait_retry
    negl %eax
    PX_RETURN
Lpx_wait_done:
    movl $0, (%r12)
    movl 100(%r12), %edx
    movl %edx, %eax
    andl $127, %eax
    testl %eax, %eax
    je Lpx_wait_normal
    cmpl $127, %eax
    je Lpx_invalid
    addl $128, %eax
    PX_RETURN
Lpx_wait_normal:
    movl %edx, %eax
    shrl $8, %eax
    andl $255, %eax
    PX_RETURN

Lpx_close:
    movl 4(%r12), %edi
    testl %edi, %edi
    js 1f
    call VM_PROC_LIB(close)
1:
    movl 8(%r12), %edi
    testl %edi, %edi
    js 2f
    call VM_PROC_LIB(close)
2:
    movl $-1, 4(%r12)
    movl $-1, 8(%r12)
    movl (%r12), %r14d
    movl $0, (%r12)
    testl %r14d, %r14d
    jle Lpx_close_done
    movl %r14d, %edi
    negl %edi
    movl $9, %esi
    call VM_PROC_LIB(kill)
Lpx_close_wait:
    movl %r14d, %edi
    leaq 100(%r12), %rsi
    xorl %edx, %edx
    call VM_PROC_LIB(waitpid)
    testl %eax, %eax
    jns Lpx_close_done
    PX_ERRNO
    cmpl $4, %eax
    je Lpx_close_wait
Lpx_close_done:
    xorl %eax, %eax
    PX_RETURN

Lpx_io_errno:
    PX_ERRNO
    // Poll interruption is reported as no readiness; V++ owns the deadline.
    cmpl $4, %eax
    je Lpx_poll_interrupted
    negl %eax
    PX_RETURN
Lpx_poll_interrupted:
    xorl %eax, %eax
    PX_RETURN
Lpx_invalid:
    movl $-22, %eax
Lpx_return:
    PX_RETURN
