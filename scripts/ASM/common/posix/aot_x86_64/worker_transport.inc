// Host pointers never cross the guest ABI. All callbacks use SysV AMD64.
.text
.globl PUBLIC(vpp_native_worker_start)
PUBLIC(vpp_native_worker_start):
    testq %rdi, %rdi
    jz Lworker_start_invalid
    pushq %rbx
    pushq %r12
    pushq %r13
    movq %rdi, %r12
    movq %rsi, %r13
    call PUBLIC(vpp_native_arena_enable_threads)
    testl %eax, %eax
    jnz Lworker_start_failed
    movl $128, %edi
    call Lworker_os_map
    testq %rax, %rax
    jz Lworker_start_failed
    movq %rax, %rbx
    movq %r12, (%rbx)
    movq %r13, 8(%rbx)
    movq %rbx, %rdi
    call Lworker_os_start
    testl %eax, %eax
    jnz Lworker_start_release
    movq %rbx, %rax
    jmp Lworker_start_return
Lworker_start_release:
    movq %rbx, %rdi
    movl $128, %esi
    call Lworker_os_unmap
Lworker_start_failed:
    xorl %eax, %eax
Lworker_start_return:
    popq %r13
    popq %r12
    popq %rbx
    ret
Lworker_start_invalid:
    xorl %eax, %eax
    ret

Lworker_entry:
    pushq %rbx
    movq %rdi, %rbx
    call PUBLIC(vpp_native_memory_base)
    testq %rax, %rax
    jz Lworker_entry_failed
    movq %rax, 24(%rbx)
    movq %rbx, %rdi
    call Lworker_os_set_current
    testl %eax, %eax
    jnz Lworker_entry_failed
    movq %rbx, %rdi
    movq 8(%rbx), %rsi
    call *(%rbx)
    movl %eax, 40(%rbx)
    movl $1, %r10d
    jmp Lworker_entry_publish
Lworker_entry_failed:
    movl $-1, %r10d
Lworker_entry_publish:
    xorl %eax, %eax
    movl $2, %edx
    lock cmpxchgl %edx, 36(%rbx)
    movl %r10d, 32(%rbx)
    xorl %eax, %eax
    popq %rbx
    ret

.globl PUBLIC(vpp_native_worker_state)
PUBLIC(vpp_native_worker_state):
    movl 32(%rdi), %eax
    ret
.globl PUBLIC(vpp_native_worker_request_cancel)
PUBLIC(vpp_native_worker_request_cancel):
    xorl %eax, %eax
    movl $1, %edx
    lock cmpxchgl %edx, 36(%rdi)
    sete %al
    movzbl %al, %eax
    ret
.globl PUBLIC(vpp_native_worker_cancelled)
PUBLIC(vpp_native_worker_cancelled):
    xorl %eax, %eax
    cmpl $1, 36(%rdi)
    sete %al
    ret
.globl PUBLIC(vpp_native_worker_join)
PUBLIC(vpp_native_worker_join):
    cmpl $0, 44(%rdi)
    jne 1f
    pushq %rbx
    movq %rdi, %rbx
    call Lworker_os_join
    testl %eax, %eax
    jnz 2f
    movl $1, 44(%rbx)
2:  popq %rbx
    ret
1:  xorl %eax, %eax
    ret

.globl PUBLIC(vpp_native_worker_read_i32)
PUBLIC(vpp_native_worker_read_i32):
    cmpl $1, 32(%rdi)
    jne Lworker_read_failed
    cmpl $(VPP_ARENA_SIZE - 4), %esi
    ja Lworker_read_failed
    testq %rdx, %rdx
    jz Lworker_read_failed
    movq 24(%rdi), %rax
    testq %rax, %rax
    jz Lworker_read_failed
    movl 60(%rdi), %r10d
    testl %r10d, %r10d
    jz 1f
    movl 40(%rdi), %r11d
    testl %r11d, %r11d
    jns 2f
    negl %r11d
2:  subl %r11d, %esi
    jb Lworker_read_failed
    subl $4, %r10d
    cmpl %r10d, %esi
    ja Lworker_read_failed
1:  movl %esi, %esi
    movl (%rax,%rsi), %eax
    movl %eax, (%rdx)
    xorl %eax, %eax
    ret
.globl PUBLIC(vpp_native_worker_result)
PUBLIC(vpp_native_worker_result):
    cmpl $1, 32(%rdi)
    jne Lworker_read_failed
    testq %rsi, %rsi
    jz Lworker_read_failed
    movl 40(%rdi), %eax
    movl %eax, (%rsi)
    xorl %eax, %eax
    ret
Lworker_read_failed:
    movl $-1, %eax
    ret

.globl PUBLIC(vpp_native_worker_dispose)
PUBLIC(vpp_native_worker_dispose):
    cmpl $0, 44(%rdi)
    je Lworker_read_failed
    pushq %rbx
    movq %rdi, %rbx
    movq 24(%rbx), %rdi
    testq %rdi, %rdi
    jz 1f
    movl 60(%rbx), %esi
    testl %esi, %esi
    jnz 2f
    movl $VPP_ARENA_SIZE, %esi
2:  call Lworker_os_unmap
    testl %eax, %eax
    jnz 3f
    movq $0, 24(%rbx)
1:  movq %rbx, %rdi
    call Lworker_os_destroy
    testl %eax, %eax
    jnz 3f
    movq %rbx, %rdi
    movl $128, %esi
    call Lworker_os_unmap
3:  popq %rbx
    ret

.globl PUBLIC(vpp_native_worker_compact_result)
PUBLIC(vpp_native_worker_compact_result):
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rdi, %rbx
    movl %esi, %r12d
    movq 24(%rbx), %r13
    testq %r13, %r13
    jz Lworker_compact_return
    xorl %r14d, %r14d
    xorl %r15d, %r15d
    cmpl $-1, %r12d
    je Lworker_compact_release
    movl %r12d, %eax
    testl %eax, %eax
    jns 1f
    negl %eax
1:  cmpl $8388624, %eax
    jb Lworker_compact_return
    cmpl $119999984, %eax
    ja Lworker_compact_return
    leaq (%r13,%rax), %r14
    cmpl $1464881153, (%r14)
    jne Lworker_compact_return
    movl 4(%r14), %r15d
    cmpl $16, %r15d
    jb Lworker_compact_return
    cmpl $31000000, %r15d
    ja Lworker_compact_return
    addq %r15, %rax
    cmpq $120000000, %rax
    ja Lworker_compact_return
    movl %r15d, %edi
    call Lworker_os_map
    testq %rax, %rax
    jz Lworker_compact_return
    movq %r14, %rsi
    movq %rax, %r14
    movq %rax, %rdi
    movl %r15d, %ecx
    cld
    rep movsb
Lworker_compact_release:
    xorl %edi, %edi
    call Lworker_os_set_arena
    testl %eax, %eax
    jnz Lworker_compact_fallback
    movq %r13, %rdi
    movl $VPP_ARENA_SIZE, %esi
    call Lworker_os_unmap
    testl %eax, %eax
    jnz Lworker_compact_fallback
    movq %r14, 24(%rbx)
    movl %r15d, 60(%rbx)
    jmp Lworker_compact_return
Lworker_compact_fallback:
    testq %r14, %r14
    jz Lworker_compact_return
    movq %r14, %rdi
    movl %r15d, %esi
    call Lworker_os_unmap
Lworker_compact_return:
    movl %r12d, %eax
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    ret
