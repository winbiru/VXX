// Freestanding integration test: callbacks execute only production ASM/syscalls.
#define VPP_LINUX_SYSCALL_PROBE_ONLY 1
#include "../../ASM/linux/vpp_linux_runtime_core_x86_64.S"
#define CHECK_ZERO testl %eax,%eax; jnz Ltest_fail
#define CHECK_PTR testq %rax,%rax; jz Ltest_fail
.text
.globl _start
_start:
    andq $-16, %rsp
    call Ltest_main
    movl %eax, %edi
    movl $231, %eax
    syscall
    ud2
Ltest_main:
    // 24 simultaneous callbacks, independent arenas, atomic cancellation.
    pushq %rbx
    pushq %r12
    pushq %r13
    movl $1, Ltest_phase(%rip)
    call vpp_native_memory_base
    CHECK_PTR
    movq %rax, Ltest_root(%rip)
    movl $99, 8388608(%rax)
    xorl %ebx, %ebx
1:  leaq Ltest_callback(%rip), %rdi
    leal 1(%rbx), %esi
    call vpp_native_worker_start
    CHECK_PTR
    leaq Ltest_records(%rip), %r12
    movq %rax, (%r12,%rbx,8)
    incl %ebx
    cmpl $24, %ebx
    jb 1b
    movl $-1, %edi
    call vpp_native_os_close
    cmpl $-1, %eax
    jne Ltest_fail
2:  cmpl $24, Ltest_ready(%rip)
    je 3f
    pause
    jmp 2b
3:  xorl %ebx, %ebx
4:  movq (%r12,%rbx,8), %rdi
    testl $1, %ebx
    jnz 5f
    call vpp_native_worker_request_cancel
    cmpl $1, %eax
    jne Ltest_fail
5:  incl %ebx
    cmpl $24, %ebx
    jb 4b
    movl $1, Ltest_release(%rip)
    xorl %ebx, %ebx
6:  movq (%r12,%rbx,8), %rdi
    call vpp_native_worker_join
    CHECK_ZERO
    movq (%r12,%rbx,8), %rdi
    leaq Ltest_value(%rip), %rsi
    call vpp_native_worker_result
    CHECK_ZERO
    leal 1(%rbx), %eax
    cmpl %eax, Ltest_value(%rip)
    jne Ltest_fail
    movq (%r12,%rbx,8), %rdi
    call vpp_native_worker_dispose
    CHECK_ZERO
    incl %ebx
    cmpl $24, %ebx
    jb 6b
    cmpl $0, Ltest_errors(%rip)
    jne Ltest_fail
    call vpp_native_memory_current
    cmpq Ltest_root(%rip), %rax
    jne Ltest_fail
    cmpl $99, 8388608(%rax)
    jne Ltest_fail
    call vpp_native_os_errno
    cmpl $9, %eax
    jne Ltest_fail
    // 135 compact snapshots; signed failures, repeated copies and reaping.
    movl $2, Ltest_phase(%rip)
    movl $11, %edi
    call vpp_native_worker_service
    CHECK_ZERO
    leaq Ltest_snapshot_callback(%rip), %rdi
    call vpp_native_worker_set_entry
    CHECK_ZERO
    xorl %ebx, %ebx
7:  movq Ltest_root(%rip), %rax
    movl %ebx, 9000000(%rax)
    movl $1, %edi
    movl $9000000, %esi
    movl $4, %edx
    call vpp_native_worker_service
    testl %eax, %eax
    jle Ltest_fail
    movl %eax, %r13d
    // Caller may overwrite its input immediately after spawn returns.
    movq Ltest_root(%rip), %rax
    movl $-99, 9000000(%rax)
8:  movl $2, %edi
    movl %r13d, %esi
    call vpp_native_worker_service
    testl %eax, %eax
    jnz 9f
    pause
    jmp 8b
9:  movl $1, %ecx
    testl $1, %ebx
    jz 10f
    movl $3, %ecx
10: cmpl %ecx, %eax
    jne Ltest_fail
    movl %r13d, %edi
    call vpp_native_worker_registry_record
    CHECK_PTR
    cmpl $1, 44(%rax)
    jne Ltest_fail
    cmpl $20, 60(%rax)
    jne Ltest_fail
    movq 24(%rax), %rcx
    cmpl %ebx, 16(%rcx)
    jne Ltest_fail
    // old arena's interior must no longer be mapped (mincore => ENOMEM).
    movq Ltest_old_arena(%rip), %rdi
    addq $33554432, %rdi
    movl $4096, %esi
    leaq Ltest_value(%rip), %rdx
    movl $27, %eax
    syscall
    cmpq $-12, %rax
    jne Ltest_fail
    movl $8, %edi
    movl %r13d, %esi
    call vpp_native_worker_service
    movl $9000000, %ecx
    testl $1, %ebx
    jz 11f
    negl %ecx
11: cmpl %ecx, %eax
    jne Ltest_fail
    movl $2, %r12d
12: movl %r13d, %edi
    movl $9000000, %esi
    movl $20, %edx
    movl $9100000, %ecx
    call vpp_native_worker_registry_copy_result
    CHECK_ZERO
    movq Ltest_root(%rip), %rax
    cmpl $0x57505001, 9100000(%rax)
    jne Ltest_fail
    cmpl %ebx, 9100016(%rax)
    jne Ltest_fail
    movl $0, 9100016(%rax)
    decl %r12d
    jnz 12b
    incl %ebx
    cmpl $135, %ebx
    jb 7b
    // Nested workers and wait-cycle detection are handled by the same registry.
    movl $3, Ltest_phase(%rip)
    leaq Ltest_nested_callback(%rip), %rdi
    xorl %esi, %esi
    call vpp_native_worker_registry_start
    movl %eax, %ebx
    testl %eax, %eax
    jle Ltest_fail
13: movl %ebx, %edi
    call vpp_native_worker_registry_record
    movq %rax, %r12
    movq %rax, %rdi
    call vpp_native_worker_state
    testl %eax, %eax
    jz 13b
    movq %r12, %rdi
    leaq Ltest_value(%rip), %rsi
    call vpp_native_worker_result
    CHECK_ZERO
    cmpl $42, Ltest_value(%rip)
    jne Ltest_fail
    movl %ebx, %edi
    movl %ebx, %esi
    call vpp_native_worker_registry_wait_begin
    cmpl $-2, %eax
    jne Ltest_fail
    movl $10, %edi
    call vpp_native_worker_service
    CHECK_ZERO
    movl $11, %edi
    call vpp_native_worker_service
    CHECK_ZERO
    // Root shutdown cancels and joins an active callback, then permits reopen.
    movl $4, Ltest_phase(%rip)
    leaq Ltest_cancel_callback(%rip), %rdi
    xorl %esi, %esi
    call vpp_native_worker_registry_start
    testl %eax, %eax
    jle Ltest_fail
    movl $10, %edi
    call vpp_native_worker_service
    CHECK_ZERO
    cmpl $1, Ltest_cancel_seen(%rip)
    jne Ltest_fail
#ifdef VPP_STANDALONE_VM_WORKER
    movl $5, Ltest_phase(%rip)
    movl $12, %edi
    call vpp_native_worker_service
    CHECK_ZERO
    movq Ltest_root(%rip), %rax
    movl $2468, 9000000(%rax)
    movl $1, %edi
    movl $9000000, %esi
    movl $4, %edx
    call vpp_native_worker_service
    testl %eax, %eax
    jle Ltest_fail
    movl %eax, %ebx
1:  movl $2, %edi
    movl %ebx, %esi
    call vpp_native_worker_service
    testl %eax, %eax
    jz 1b
    cmpl $1, %eax
    jne Ltest_fail
    movl $10, %edi
    call vpp_native_worker_service
    CHECK_ZERO
#endif
    xorl %eax, %eax
    popq %r13
    popq %r12
    popq %rbx
    ret
Ltest_fail:
    movl Ltest_phase(%rip), %edi
    movl $231, %eax
    syscall
    ud2
Ltest_callback:
    pushq %rbx
    pushq %r12
    pushq %r13
    movq %rdi, %rbx
    movl %esi, %r12d
    call vpp_native_worker_current
    cmpq %rbx, %rax
    jne Ltest_callback_fail
    call vpp_native_memory_current
    CHECK_PTR
    cmpq Ltest_root(%rip), %rax
    je Ltest_callback_fail
    cmpl $0, 8388608(%rax)
    jne Ltest_callback_fail
    movq %rax, %r13
    movl %r12d, 8388608(%rax)
    movl $-1, %edi
    call vpp_native_memory_read_i32
    cmpl $-1, %eax
    jne Ltest_callback_fail
    call vpp_native_os_errno
    cmpl $22, %eax
    jne Ltest_callback_fail
    movl %r12d, %edi
    call Lworker_set_errno
    lock incl Ltest_ready(%rip)
1:  cmpl $0, Ltest_release(%rip)
    jne 2f
    pause
    jmp 1b
2:  call vpp_native_memory_current
    cmpq %r13, %rax
    jne Ltest_callback_fail
    cmpl %r12d, 8388608(%rax)
    jne Ltest_callback_fail
    call vpp_native_os_errno
    cmpl %r12d, %eax
    jne Ltest_callback_fail
    testl $1, %r12d
    jz 4f
3:  movq %rbx, %rdi
    call vpp_native_worker_cancelled
    testl %eax, %eax
    jnz 4f
    pause
    jmp 3b
4:  movl %r12d, %eax
    popq %r13
    popq %r12
    popq %rbx
    ret
Ltest_callback_fail:
    lock incl Ltest_errors(%rip)
    lock incl Ltest_ready(%rip)
    jmp 4b
Ltest_snapshot_callback:
    pushq %rbx
    movq %rdi, %rbx
    call vpp_native_memory_current
    movq %rax, Ltest_old_arena(%rip)
    movl 89000000(%rax), %edx
    movl $0x57505001, 9000000(%rax)
    movl $20, 9000004(%rax)
    movl %edx, 9000016(%rax)
    movl $9000000, %esi
    testl $1, %edx
    jz 1f
    negl %esi
1:  movq %rbx, %rdi
    call vpp_native_worker_compact_result
    popq %rbx
    ret
Ltest_nested_callback:
    pushq %rbx
    leaq Ltest_leaf_callback(%rip), %rdi
    xorl %esi, %esi
    call vpp_native_worker_start
    CHECK_PTR
    movq %rax, %rbx
    movq %rax, %rdi
    call vpp_native_worker_join
    CHECK_ZERO
    movq %rbx, %rdi
    leaq Ltest_value(%rip), %rsi
    call vpp_native_worker_result
    CHECK_ZERO
    movq %rbx, %rdi
    call vpp_native_worker_dispose
    CHECK_ZERO
    movl Ltest_value(%rip), %eax
    popq %rbx
    ret
Ltest_leaf_callback:
    movl $42, %eax
    ret
Ltest_cancel_callback:
    pushq %rbx
    movq %rdi, %rbx
1:  movq %rbx, %rdi
    call vpp_native_worker_cancelled
    testl %eax, %eax
    jnz 2f
    pause
    jmp 1b
2:  movl $1, Ltest_cancel_seen(%rip)
    xorl %eax, %eax
    popq %rbx
    ret
.bss
.p2align 3
Ltest_root: .zero 8
Ltest_old_arena: .zero 8
Ltest_records: .zero 192
Ltest_ready: .zero 4
Ltest_release: .zero 4
Ltest_errors: .zero 4
Ltest_value: .zero 4
Ltest_phase: .zero 4
Ltest_cancel_seen: .zero 4
.section .note.GNU-stack,"",@progbits

#ifdef VPP_STANDALONE_VM_WORKER
.text
.globl vpp_fn_vm_worker_entry
vpp_fn_vm_worker_entry:
    cmpl $89000000, %edi
    jne Ltest_fail
    subq $8, %rsp
    call vpp_native_memory_current
    cmpl $2468, 89000000(%rax)
    jne Ltest_fail
    movl $0x57505001, 9000000(%rax)
    movl $16, 9000004(%rax)
    movl $9000000, %eax
    addq $8, %rsp
    ret
#endif

.bss
.p2align 3
.globl vpp_linux_argc, vpp_linux_argv, vpp_linux_envp
vpp_linux_argc: .zero 8
vpp_linux_argv: .zero 8
vpp_linux_envp: .zero 8
