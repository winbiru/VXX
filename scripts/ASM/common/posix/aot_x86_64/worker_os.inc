// OS boundary for SysV workers. Linux remains a static, libc-free runtime.
.text
Lworker_os_map:
    movq %rdi, %rsi
    xorl %edi, %edi
    movl $3, %edx
    movq $-1, %r8
    xorl %r9d, %r9d
#ifdef __APPLE__
    movl $0x1002, %ecx
    subq $8, %rsp
    call LIB(mmap)
    addq $8, %rsp
    cmpq $-1, %rax
    je 1f
#else
    movl $0x22, %r10d
    movl $9, %eax
    syscall
    cmpq $-4095, %rax
    jae 1f
#endif
    ret
1:  xorl %eax, %eax
    ret
Lworker_os_unmap:
#ifdef __APPLE__
    jmp LIB(munmap)
#else
    movl $11, %eax
    syscall
    ret
#endif

#ifdef __APPLE__
Lworker_os_enable:
    pushq %rbx
    movq %rdi, %rbx
    leaq Lworker_arena_key(%rip), %rdi
    xorl %esi, %esi
    call LIB(pthread_key_create)
    testl %eax, %eax
    jnz 3f
    movq Lworker_arena_key(%rip), %rdi
    movq %rbx, %rsi
    call LIB(pthread_setspecific)
    testl %eax, %eax
    jnz 2f
    leaq Lworker_current_key(%rip), %rdi
    xorl %esi, %esi
    call LIB(pthread_key_create)
    testl %eax, %eax
    jz 3f
2:  movq Lworker_arena_key(%rip), %rdi
    call LIB(pthread_key_delete)
    movl $-1, %eax
3:  popq %rbx
    ret
Lworker_os_arena:
    movq Lworker_arena_key(%rip), %rdi
    jmp LIB(pthread_getspecific)
Lworker_os_set_arena:
    movq %rdi, %rsi
    movq Lworker_arena_key(%rip), %rdi
    jmp LIB(pthread_setspecific)
Lworker_os_current:
    movq Lworker_current_key(%rip), %rdi
    jmp LIB(pthread_getspecific)
Lworker_os_set_current:
    movq %rdi, %rsi
    movq Lworker_current_key(%rip), %rdi
    jmp LIB(pthread_setspecific)
Lworker_os_start:
    pushq %rbx
    subq $128, %rsp
    movq %rdi, %rbx
    movq %rsp, %rdi
    call LIB(pthread_attr_init)
    testl %eax, %eax
    jnz 2f
    movq %rsp, %rdi
    movl $67108864, %esi
    call LIB(pthread_attr_setstacksize)
    testl %eax, %eax
    jnz 1f
    leaq 16(%rbx), %rdi
    movq %rsp, %rsi
    leaq Lworker_entry(%rip), %rdx
    movq %rbx, %rcx
    call LIB(pthread_create)
1:  movl %eax, %ebx
    movq %rsp, %rdi
    call LIB(pthread_attr_destroy)
    movl %ebx, %eax
2:  addq $128, %rsp
    popq %rbx
    ret
Lworker_os_join:
    movq 16(%rdi), %rdi
    xorl %esi, %esi
    jmp LIB(pthread_join)
Lworker_os_destroy:
    xorl %eax, %eax
    ret
.section __DATA,__bss
.p2align 3
Lworker_arena_key: .zero 8
Lworker_current_key: .zero 8
#else
// GS is private to this runtime; FS (libc TLS) is never overwritten. GET_GS
// also makes queries from unrelated host threads safe (a null GS => no worker).
// Raw clone callbacks must use only V++/ASM/syscalls, never host libc callbacks.
Lworker_os_context:
    subq $24, %rsp
    movl $0x1004, %edi             // ARCH_GET_GS
    movq %rsp, %rsi
    movl $158, %eax
    syscall
    testq %rax, %rax
    jnz 1f
    movq (%rsp), %rax
    addq $24, %rsp
    ret
1:  xorl %eax, %eax
    addq $24, %rsp
    ret
Lworker_os_enable:
    pushq %rbx
    movq %rdi, %rbx
    call Lworker_os_context
    testq %rax, %rax
    jnz 1f                       // Do not replace a host's existing GS context.
    leaq Lworker_root_context(%rip), %rsi
    movq %rbx, 8(%rsi)
    movl $0x1001, %edi             // ARCH_SET_GS
    movl $158, %eax
    syscall
    popq %rbx
    ret
1:  movl $-1, %eax
    popq %rbx
    ret
Lworker_os_arena:
    subq $8, %rsp
    call Lworker_os_context
    testq %rax, %rax
    jz 1f
    movq 8(%rax), %rax
1:  addq $8, %rsp
    ret
Lworker_os_current:
    subq $8, %rsp
    call Lworker_os_context
    testq %rax, %rax
    jz 1f
    movq (%rax), %rax
1:  addq $8, %rsp
    ret
Lworker_os_set_arena:
    pushq %rbx
    movq %rdi, %rbx
    call Lworker_os_context
    testq %rax, %rax
    jz 1f
    movq %rbx, 8(%rax)
    xorl %eax, %eax
    popq %rbx
    ret
1:  movl $-1, %eax
    popq %rbx
    ret
Lworker_os_set_current:
    pushq %rbx
    movq %rdi, %rbx
    call Lworker_os_context
    testq %rax, %rax
    jz 1f
    movq %rbx, (%rax)
    xorl %eax, %eax
    popq %rbx
    ret
1:  movl $-1, %eax
    popq %rbx
    ret

// record+64 stack mapping, +80 kernel clear_child_tid, +96 worker, +104 arena.
// Stack has a protected bottom page; clone returns on an independent stack.
Lworker_os_start:
    pushq %rbx
    movq %rdi, %rbx
    movl $67112960, %edi           // 64 MiB stack + guard
    call Lworker_os_map
    testq %rax, %rax
    jz Lworker_os_start_fail
    movq %rax, 64(%rbx)
    movq %rax, %rdi
    movl $4096, %esi
    xorl %edx, %edx
    movl $10, %eax                 // mprotect(PROT_NONE)
    syscall
    testq %rax, %rax
    jnz Lworker_os_start_release
    movq %rbx, 96(%rbx)
    movq 64(%rbx), %rsi
    addq $67112960, %rsi
    subq $16, %rsi
    movq %rbx, (%rsi)
    // VM|FS|FILES|SIGHAND|THREAD|SYSVSEM|PARENT_SETTID|CHILD_CLEARTID
    movl $0x350f00, %edi
    leaq 80(%rbx), %rdx
    movq %rdx, %r10
    xorl %r8d, %r8d
    movl $56, %eax                 // clone
    syscall
    testq %rax, %rax
    jz Lworker_os_child
    js Lworker_os_start_release
    movq %rax, 16(%rbx)
    xorl %eax, %eax
    popq %rbx
    ret
Lworker_os_child:
    movq (%rsp), %rbx
    leaq 96(%rbx), %rsi
    movl $0x1001, %edi
    movl $158, %eax
    syscall
    testq %rax, %rax
    jnz Lworker_os_child_failed
    movq %rbx, %rdi
    call Lworker_entry
    jmp Lworker_os_child_exit
Lworker_os_child_failed:
    movl $2, 36(%rbx)
    movl $-1, 32(%rbx)
Lworker_os_child_exit:
    xorl %edi, %edi
    movl $60, %eax                 // exit this thread, never exit_group
    syscall
    ud2
Lworker_os_start_release:
    movq 64(%rbx), %rdi
    movl $67112960, %esi
    call Lworker_os_unmap
    movq $0, 64(%rbx)
Lworker_os_start_fail:
    movl $-1, %eax
    popq %rbx
    ret
Lworker_os_join:
    leaq 80(%rdi), %r8
1:  movl (%r8), %edx
    testl %edx, %edx
    jz 2f
    movq %r8, %rdi
    xorl %esi, %esi                // shared FUTEX_WAIT, kernel wakes on exit
    xorl %r10d, %r10d
    movl $202, %eax
    syscall
    testq %rax, %rax
    jns 1b
    cmpq $-4, %rax                 // EINTR
    je 1b
    cmpq $-11, %rax                // EAGAIN: exited before wait
    je 1b
    ret
2:  xorl %eax, %eax
    ret
Lworker_os_destroy:
    pushq %rbx
    movq %rdi, %rbx
    movq 64(%rbx), %rdi
    testq %rdi, %rdi
    jz 1f
    movl $67112960, %esi
    call Lworker_os_unmap
    testl %eax, %eax
    jnz 2f
    movq $0, 64(%rbx)
1:  xorl %eax, %eax
2:  popq %rbx
    ret
.bss
.p2align 3
Lworker_root_context: .zero 24
#endif
.text
