// Host-only worker records; same layout and ownership as macOS worker.inc.
// Microsoft x64 ABI; _beginthreadex initializes CRT state for worker I/O.
.text
.p2align 4
.globl vpp_native_worker_start
vpp_native_worker_start:
    testq %rcx, %rcx
    jz Lworker_start_invalid
    subq $72, %rsp
    movq %rcx, 48(%rsp)
    movq %rdx, 56(%rsp)
    call vpp_native_arena_enable_threads
    testl %eax, %eax
    jnz Lworker_start_failed
    xorl %ecx, %ecx
    movl $64, %edx
    movl $0x3000, %r8d
    movl $4, %r9d
    call VirtualAlloc
    testq %rax, %rax
    jz Lworker_start_failed
    movq %rax, 64(%rsp)
    movq 48(%rsp), %r10
    movq %r10, (%rax)
    movq 56(%rsp), %r10
    movq %r10, 8(%rax)
    xorl %ecx, %ecx
    xorl %edx, %edx
    leaq Lworker_entry(%rip), %r8
    movq %rax, %r9
    movq $0, 32(%rsp)
    movq $0, 40(%rsp)
    call _beginthreadex
    testq %rax, %rax
    jz Lworker_start_release
    movq 64(%rsp), %r10
    movq %rax, 16(%r10)
    movq %r10, %rax
    jmp Lworker_start_return
Lworker_start_release:
    movq 64(%rsp), %rcx
    xorl %edx, %edx
    movl $0x8000, %r8d
    call VirtualFree
Lworker_start_failed:
    xorq %rax, %rax
Lworker_start_return:
    addq $72, %rsp
    ret
Lworker_start_invalid:
    xorq %rax, %rax
    ret

.p2align 4
Lworker_entry:
    subq $56, %rsp
    movq %rcx, 32(%rsp)
    call vpp_native_memory_base
    movq 32(%rsp), %rcx
    testq %rax, %rax
    jz Lworker_entry_failed
    movq %rax, 24(%rcx)
    movq %rcx, %rdx
    movl vpp_native_worker_tls_key(%rip), %ecx
    call TlsSetValue
    movq 32(%rsp), %rcx
    testl %eax, %eax
    jz Lworker_entry_failed
    movq 8(%rcx), %rdx
    call *(%rcx)
    movq 32(%rsp), %rcx
    movl %eax, 40(%rcx)
    movl $1, %r10d
    jmp Lworker_entry_publish
Lworker_entry_failed:
    movl $-1, %r10d
Lworker_entry_publish:
    // Seal completion; a successful cancellation retains ownership.
    xorl %eax, %eax
    movl $2, %edx
    lock cmpxchgl %edx, 36(%rcx)
    movl %r10d, 32(%rcx)
Lworker_entry_return:
    xorl %eax, %eax
    addq $56, %rsp
    ret

// x86 aligned loads/stores provide acquire/release ordering.
.globl vpp_native_worker_state
vpp_native_worker_state:
    movl 32(%rcx), %eax
    ret
.globl vpp_native_worker_request_cancel
vpp_native_worker_request_cancel:
    xorl %eax, %eax
    movl $1, %edx
    lock cmpxchgl %edx, 36(%rcx)
    sete %al
    movzbl %al, %eax
    ret
.globl vpp_native_worker_cancelled
vpp_native_worker_cancelled:
    xorl %eax, %eax
    cmpl $1, 36(%rcx)
    sete %al
    ret

.globl vpp_native_worker_join
vpp_native_worker_join:
    cmpl $0, 44(%rcx)
    jne Lworker_join_done
    subq $56, %rsp
    movq %rcx, 32(%rsp)
    movq 16(%rcx), %rcx
    movl $-1, %edx
    call WaitForSingleObject
    testl %eax, %eax
    jnz Lworker_join_failed
    movq 32(%rsp), %rcx
    movq 16(%rcx), %rcx
    call CloseHandle
    testl %eax, %eax
    jz Lworker_join_failed
    movq 32(%rsp), %rcx
    movl $1, 44(%rcx)
    addq $56, %rsp
Lworker_join_done:
    xorl %eax, %eax
    ret
Lworker_join_failed:
    addq $56, %rsp
    movl $-1, %eax
    ret

.globl vpp_native_worker_read_i32
vpp_native_worker_read_i32:
    cmpl $1, 32(%rcx)
    jne Lworker_read_failed
    cmpl $(VPP_ARENA_SIZE - 4), %edx
    ja Lworker_read_failed
    testq %r8, %r8
    jz Lworker_read_failed
    movq 24(%rcx), %rax
    testq %rax, %rax
    jz Lworker_read_failed
    movl 60(%rcx), %r10d
    testl %r10d, %r10d
    jz 1f
    movl 40(%rcx), %r11d
    testl %r11d, %r11d
    jns 2f
    negl %r11d
2:  subl %r11d, %edx
    jb Lworker_read_failed
    subl $4, %r10d
    cmpl %r10d, %edx
    ja Lworker_read_failed
1:  movl %edx, %edx
    movl (%rax,%rdx), %eax
    movl %eax, (%r8)
    xorl %eax, %eax
    ret
Lworker_read_failed:
    movl $-1, %eax
    ret
.globl vpp_native_worker_result
vpp_native_worker_result:
    cmpl $1, 32(%rcx)
    jne Lworker_read_failed
    testq %rdx, %rdx
    jz Lworker_read_failed
    movl 40(%rcx), %eax
    movl %eax, (%rdx)
    xorl %eax, %eax
    ret

.globl vpp_native_worker_dispose
vpp_native_worker_dispose:
    cmpl $0, 44(%rcx)
    je Lworker_dispose_invalid
    subq $56, %rsp
    movq %rcx, 32(%rsp)
    movq 24(%rcx), %rcx
    testq %rcx, %rcx
    jz Lworker_dispose_record
    xorl %edx, %edx
    movl $0x8000, %r8d
    call VirtualFree
    testl %eax, %eax
    jz Lworker_dispose_failed
    movq 32(%rsp), %rcx
    movq $0, 24(%rcx)
Lworker_dispose_record:
    movq 32(%rsp), %rcx
    xorl %edx, %edx
    movl $0x8000, %r8d
    call VirtualFree
    testl %eax, %eax
    jz Lworker_dispose_failed
    addq $56, %rsp
    xorl %eax, %eax
    ret
Lworker_dispose_failed:
    addq $56, %rsp
Lworker_dispose_invalid:
    movl $-1, %eax
    ret

// Trusted guest callbacks call this after leaving guest execution, before
// the terminal state is published. Ordinary callbacks retain their arena.
// record+60 is the compact packet length, record+40 the signed guest offset.
.globl vpp_native_worker_compact_result
vpp_native_worker_compact_result:
    pushq %rbx
    pushq %rsi
    pushq %rdi
    pushq %r12
    pushq %r13
    pushq %r14
    subq $40, %rsp
    movq %rcx, %rbx
    movl %edx, %r12d
    movq 24(%rbx), %r13
    testq %r13, %r13
    jz Lworker_compact_return
    xorq %r14, %r14
    xorl %edi, %edi
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
    leaq (%r13,%rax), %rsi
    cmpl $1464881153, (%rsi)
    jne Lworker_compact_return
    movl 4(%rsi), %edi
    cmpl $16, %edi
    jb Lworker_compact_return
    cmpl $31000000, %edi
    ja Lworker_compact_return
    addq %rdi, %rax
    cmpq $120000000, %rax
    ja Lworker_compact_return
    xorl %ecx, %ecx
    movl %edi, %edx
    movl $0x3000, %r8d
    movl $4, %r9d
    call VirtualAlloc
    testq %rax, %rax
    jz Lworker_compact_return
    movq %rax, %r14
    movl %edi, 32(%rsp)
    movl %edi, %ecx
    movq %rax, %rdi
    cld
    rep movsb
    movl 32(%rsp), %edi
Lworker_compact_release:
    movl vpp_native_arena_tls_key(%rip), %ecx
    xorl %edx, %edx
    call TlsSetValue
    testl %eax, %eax
    jz Lworker_compact_fallback
    movq %r13, %rcx
    xorl %edx, %edx
    movl $0x8000, %r8d
    call VirtualFree
    testl %eax, %eax
    jz Lworker_compact_fallback
    movq %r14, 24(%rbx)
    movl %edi, 60(%rbx)
    jmp Lworker_compact_return
Lworker_compact_fallback:
    testq %r14, %r14
    jz Lworker_compact_return
    movq %r14, %rcx
    xorl %edx, %edx
    movl $0x8000, %r8d
    call VirtualFree
Lworker_compact_return:
    movl %r12d, %eax
    addq $40, %rsp
    popq %r14
    popq %r13
    popq %r12
    popq %rdi
    popq %rsi
    popq %rbx
    ret

// Root-owned worker registry, same protocol as the ARM64 implementation.
// All pointers remain in native storage, and all records outlive waiters.
.bss
.p2align 3
Lworker_registry_slots: .zero 32768
.p2align 2
Lworker_registry_lock_word: .zero 4
Lworker_registry_count: .zero 4
Lworker_registry_closing: .zero 4
.text
.p2align 4
Lworker_registry_lock:
    movl $1, %eax
1:  xchgl %eax, Lworker_registry_lock_word(%rip)
    testl %eax, %eax
    jz 2f
    pause
    jmp 1b
2:  ret
Lworker_registry_unlock:
    movl $0, Lworker_registry_lock_word(%rip)
    ret
// Internal leaf: ecx = handle, rax = record. Lock is held.
Lworker_registry_find:
    cmpl $1, %ecx
    jl 1f
    cmpl Lworker_registry_count(%rip), %ecx
    jg 1f
    decl %ecx
    leaq Lworker_registry_slots(%rip), %rax
    movq (%rax,%rcx,8), %rax
    ret
1:  xorl %eax, %eax
    ret

.globl vpp_native_worker_registry_start
vpp_native_worker_registry_start:
    pushq %rbx
    pushq %rsi
    pushq %rdi
    subq $32, %rsp
    movq %rcx, %rsi
    movq %rdx, %rdi
    call Lworker_registry_lock
    cmpl $0, Lworker_registry_closing(%rip)
    jne Lworker_registry_start_fail
    movl Lworker_registry_count(%rip), %ebx
    cmpl $4096, %ebx
    jge Lworker_registry_start_fail
    movq %rsi, %rcx
    movq %rdi, %rdx
    call vpp_native_worker_start
    testq %rax, %rax
    jz Lworker_registry_start_fail
    leaq Lworker_registry_slots(%rip), %r10
    movq %rax, (%r10,%rbx,8)
    incl %ebx
    movl %ebx, 48(%rax)
    movl %ebx, Lworker_registry_count(%rip)
    jmp Lworker_registry_start_done
Lworker_registry_start_fail:
    movl $-1, %ebx
Lworker_registry_start_done:
    call Lworker_registry_unlock
    movl %ebx, %eax
    addq $32, %rsp
    popq %rdi
    popq %rsi
    popq %rbx
    ret

.globl vpp_native_worker_registry_record
vpp_native_worker_registry_record:
    subq $40, %rsp
    call Lworker_registry_lock
    call Lworker_registry_find
    movq %rax, %r10
    call Lworker_registry_unlock
    movq %r10, %rax
    addq $40, %rsp
    ret

.globl vpp_native_worker_registry_self
vpp_native_worker_registry_self:
    subq $40, %rsp
    call Lworker_registry_lock
    movl 48(%rcx), %r10d
    call Lworker_registry_unlock
    movl %r10d, %eax
    addq $40, %rsp
    ret

.globl vpp_native_worker_registry_wait_begin
vpp_native_worker_registry_wait_begin:
    pushq %rbx
    pushq %rsi
    pushq %rdi
    subq $48, %rsp
    movl %ecx, %ebx
    movl %edx, %esi
    call Lworker_registry_lock
    movl %esi, %ecx
    call Lworker_registry_find
    testq %rax, %rax
    jz Lworker_registry_wait_invalid
    movq %rax, %rdi
    testl %ebx, %ebx
    jz Lworker_registry_wait_ok
    cmpl %esi, %ebx
    je Lworker_registry_wait_cycle
    movl %ebx, %ecx
    call Lworker_registry_find
    testq %rax, %rax
    jz Lworker_registry_wait_invalid
    movq %rax, 32(%rsp)
    cmpl $0, 52(%rax)
    jne Lworker_registry_wait_cycle
Lworker_registry_wait_walk:
    movl 52(%rdi), %ecx
    testl %ecx, %ecx
    jz Lworker_registry_wait_commit
    cmpl %ebx, %ecx
    je Lworker_registry_wait_cycle
    call Lworker_registry_find
    testq %rax, %rax
    jz Lworker_registry_wait_invalid
    movq %rax, %rdi
    jmp Lworker_registry_wait_walk
Lworker_registry_wait_commit:
    movq 32(%rsp), %rax
    movl %esi, 52(%rax)
Lworker_registry_wait_ok:
    xorl %ebx, %ebx
    jmp Lworker_registry_wait_return
Lworker_registry_wait_invalid:
    movl $-1, %ebx
    jmp Lworker_registry_wait_return
Lworker_registry_wait_cycle:
    movl $-2, %ebx
Lworker_registry_wait_return:
    call Lworker_registry_unlock
    movl %ebx, %eax
    addq $48, %rsp
    popq %rdi
    popq %rsi
    popq %rbx
    ret

.globl vpp_native_worker_registry_wait_end
vpp_native_worker_registry_wait_end:
    testl %ecx, %ecx
    jz 1f
    subq $40, %rsp
    call Lworker_registry_lock
    call Lworker_registry_find
    testq %rax, %rax
    jz 2f
    movl $0, 52(%rax)
2:  call Lworker_registry_unlock
    addq $40, %rsp
1:  ret

.globl vpp_native_worker_registry_shutdown
vpp_native_worker_registry_shutdown:
    pushq %rbx
    pushq %rsi
    pushq %rdi
    subq $32, %rsp
    call Lworker_registry_lock
    movl $1, Lworker_registry_closing(%rip)
    movl Lworker_registry_count(%rip), %ebx
    leaq Lworker_registry_slots(%rip), %rdi
    xorl %esi, %esi
Lworker_registry_shutdown_cancel:
    cmpl %ebx, %esi
    jge Lworker_registry_shutdown_unlock
    movq (%rdi,%rsi,8), %rcx
    testq %rcx, %rcx
    jz 1f
    call vpp_native_worker_request_cancel
1:  incl %esi
    jmp Lworker_registry_shutdown_cancel
Lworker_registry_shutdown_unlock:
    call Lworker_registry_unlock
    xorl %esi, %esi
Lworker_registry_shutdown_join:
    cmpl %ebx, %esi
    jge Lworker_registry_shutdown_dispose_begin
    movq (%rdi,%rsi,8), %rcx
    testq %rcx, %rcx
    jz 1f
    call vpp_native_worker_join
    testl %eax, %eax
    jnz Lworker_registry_shutdown_fail
1:  incl %esi
    jmp Lworker_registry_shutdown_join
Lworker_registry_shutdown_dispose_begin:
    xorl %esi, %esi
Lworker_registry_shutdown_dispose:
    cmpl %ebx, %esi
    jge Lworker_registry_shutdown_done
    movq (%rdi,%rsi,8), %rcx
    testq %rcx, %rcx
    jz 1f
    call vpp_native_worker_dispose
    testl %eax, %eax
    jnz Lworker_registry_shutdown_fail
    movq $0, (%rdi,%rsi,8)
1:  incl %esi
    jmp Lworker_registry_shutdown_dispose
Lworker_registry_shutdown_done:
    call Lworker_registry_lock
    movl $0, Lworker_registry_count(%rip)
    movq $0, Lworker_service_entry(%rip)
    call Lworker_registry_unlock
    xorl %eax, %eax
    jmp Lworker_registry_shutdown_return
Lworker_registry_shutdown_fail:
    movl $-1, %eax
Lworker_registry_shutdown_return:
    addq $32, %rsp
    popq %rdi
    popq %rsi
    popq %rbx
    ret

.globl vpp_native_worker_registry_open
vpp_native_worker_registry_open:
    subq $40, %rsp
    call Lworker_registry_lock
    cmpl $0, Lworker_registry_count(%rip)
    jne 1f
    movl $0, Lworker_registry_closing(%rip)
    xorl %r10d, %r10d
    jmp 2f
1:  movl $-1, %r10d
2:  call Lworker_registry_unlock
    movl %r10d, %eax
    addq $40, %rsp
    ret

// Same synchronous input snapshot protocol as macOS. The caller's packet
// remains live until the copy is published at record+56.
.globl vpp_native_worker_registry_start_copy
vpp_native_worker_registry_start_copy:
    testq %rcx, %rcx
    jz Lworker_copy_invalid
    cmpl $VPP_ARENA_SIZE, %r8d
    ja Lworker_copy_invalid
    movl $VPP_ARENA_SIZE, %eax
    subl %r8d, %eax
    cmpl %eax, %edx
    ja Lworker_copy_invalid
    cmpl %eax, %r9d
    ja Lworker_copy_invalid
    pushq %rbx
    subq $64, %rsp
    movq %rcx, 32(%rsp)
    movl %edx, %ebx
    movl %r8d, 48(%rsp)
    movl %r9d, 52(%rsp)
    call vpp_native_memory_base
    testq %rax, %rax
    jz Lworker_start_copy_fail
    addq %rbx, %rax
    movq %rax, 40(%rsp)
    leaq Lworker_copy_entry(%rip), %rcx
    leaq 32(%rsp), %rdx
    call vpp_native_worker_registry_start
    testl %eax, %eax
    jle Lworker_start_copy_fail
    movl %eax, %ebx
    movl %eax, %ecx
    call vpp_native_worker_registry_record
Lworker_start_copy_wait:
    cmpl $0, 56(%rax)
    jne Lworker_start_copy_done
    cmpl $0, 32(%rax)
    je 1f
    cmpl $0, 56(%rax)
    jne Lworker_start_copy_done
    movq $0, 8(%rax)
    jmp Lworker_start_copy_fail
1:  pause
    jmp Lworker_start_copy_wait
Lworker_start_copy_done:
    movl %ebx, %eax
    jmp Lworker_start_copy_return
Lworker_start_copy_fail:
    movl $-1, %eax
Lworker_start_copy_return:
    addq $64, %rsp
    popq %rbx
    ret
Lworker_copy_invalid:
    movl $-1, %eax
    ret

Lworker_copy_entry:
    pushq %rbx
    pushq %rsi
    pushq %rdi
    subq $48, %rsp
    movq %rcx, %rbx
    movq (%rdx), %rax
    movq %rax, 32(%rsp)
    movq 8(%rdx), %rsi
    movl 16(%rdx), %ecx
    movl 20(%rdx), %eax
    movl %eax, 40(%rsp)
    movq 24(%rbx), %rdi
    addq %rax, %rdi
    cld
    rep movsb
    movq $0, 8(%rbx)
    movl $1, 56(%rbx)
    movq %rbx, %rcx
    movl 40(%rsp), %edx
    call *32(%rsp)
    addq $48, %rsp
    popq %rdi
    popq %rsi
    popq %rbx
    ret

.globl vpp_native_worker_registry_copy_result
vpp_native_worker_registry_copy_result:
    cmpl $VPP_ARENA_SIZE, %r8d
    ja Lworker_copy_invalid
    movl $VPP_ARENA_SIZE, %eax
    subl %r8d, %eax
    cmpl %eax, %edx
    ja Lworker_copy_invalid
    cmpl %eax, %r9d
    ja Lworker_copy_invalid
    pushq %rbx
    pushq %rsi
    pushq %rdi
    subq $48, %rsp
    movl %edx, %esi
    movl %r8d, %ebx
    movl %r9d, %edi
    call vpp_native_worker_registry_record
    testq %rax, %rax
    jz Lworker_copy_result_fail
    cmpl $1, 32(%rax)
    jne Lworker_copy_result_fail
    movq 24(%rax), %r10
    testq %r10, %r10
    jz Lworker_copy_result_fail
    movl 60(%rax), %r11d
    testl %r11d, %r11d
    jz 1f
    movl 40(%rax), %eax
    testl %eax, %eax
    jns 2f
    negl %eax
2:  subl %eax, %esi
    jb Lworker_copy_result_fail
    subl %ebx, %r11d
    jb Lworker_copy_result_fail
    cmpl %r11d, %esi
    ja Lworker_copy_result_fail
1:  addq %r10, %rsi
    call vpp_native_memory_base
    testq %rax, %rax
    jz Lworker_copy_result_fail
    addq %rax, %rdi
    movl %ebx, %ecx
    cld
    rep movsb
    xorl %eax, %eax
    jmp Lworker_copy_result_return
Lworker_copy_result_fail:
    movl $-1, %eax
Lworker_copy_result_return:
    addq $48, %rsp
    popq %rdi
    popq %rsi
    popq %rbx
    ret

.globl vpp_native_worker_current
vpp_native_worker_current:
    cmpl $0, vpp_native_arena_tls_enabled(%rip)
    je 1f
    subq $56, %rsp
    call GetLastError
    movl %eax, 32(%rsp)
    movl vpp_native_worker_tls_key(%rip), %ecx
    call TlsGetValue
    movq %rax, 40(%rsp)
    movl 32(%rsp), %ecx
    call SetLastError
    movq 40(%rsp), %rax
    addq $56, %rsp
    ret
1:  xorq %rax, %rax
    ret

// Serialize terminal reaping with other observers and root shutdown.
Lworker_registry_reap_terminal:
    subq $56, %rsp
    movq %rcx, 32(%rsp)
    call Lworker_registry_lock
    cmpl $0, Lworker_registry_closing(%rip)
    jne 1f
    movq 32(%rsp), %rcx
    call vpp_native_worker_join
1:  call Lworker_registry_unlock
    addq $56, %rsp
    ret

// Pointer-safe worker service. The root binds the native callback outside
// the guest ABI; guest integers contain only handles or arena offsets.
.bss
.p2align 3
Lworker_service_entry: .quad 0
.text
.globl vpp_native_worker_set_entry
vpp_native_worker_set_entry:
    subq $56, %rsp
    movq %rcx, 32(%rsp)
    call vpp_native_worker_current
    testq %rax, %rax
    jnz Lworker_set_entry_invalid
    call Lworker_registry_lock
    cmpl $0, Lworker_registry_count(%rip)
    jne Lworker_set_entry_locked
    movq 32(%rsp), %rax
    movq %rax, Lworker_service_entry(%rip)
    call Lworker_registry_unlock
    xorl %eax, %eax
    jmp Lworker_set_entry_return
Lworker_set_entry_locked:
    call Lworker_registry_unlock
Lworker_set_entry_invalid:
    movl $-1, %eax
Lworker_set_entry_return:
    addq $56, %rsp
    ret

// Same operation numbers, packet staging and status contract as ARM64.
.globl vpp_native_worker_service
vpp_native_worker_service:
    pushq %rbx
    pushq %rsi
    pushq %rdi
    subq $48, %rsp
    movl %ecx, %ebx
    movl %edx, %esi
    movl %r8d, %edi
#ifdef VPP_STANDALONE_VM_WORKER
    cmpl $12, %ebx
    je Lworker_service_vm_register
#endif
    cmpl $11, %ebx
    ja Lworker_service_invalid
    testl %ebx, %ebx
    jz Lworker_service_available
    cmpl $1, %ebx
    je Lworker_service_spawn
    cmpl $4, %ebx
    jb Lworker_service_lookup
    cmpl $7, %ebx
    jbe Lworker_service_self
    cmpl $9, %ebx
    jbe Lworker_service_output
    jmp Lworker_service_root
Lworker_service_available:
    xorl %eax, %eax
    cmpq $0, Lworker_service_entry(%rip)
    setne %al
    jmp Lworker_service_return
Lworker_service_spawn:
    cmpl $31000000, %edi
    ja Lworker_service_invalid
    movq Lworker_service_entry(%rip), %rcx
    testq %rcx, %rcx
    jz Lworker_service_invalid
    movl %esi, %edx
    movl %edi, %r8d
    movl $89000000, %r9d
    call vpp_native_worker_registry_start_copy
    jmp Lworker_service_return
Lworker_service_lookup:
    movl %esi, %ecx
    call vpp_native_worker_registry_record
    testq %rax, %rax
    jz Lworker_service_invalid
    movq %rax, %rcx
    cmpl $3, %ebx
    je Lworker_service_cancel
    movq %rax, %rsi
    call vpp_native_worker_state
    testl %eax, %eax
    jz Lworker_service_return
    movl %eax, %edi
    movq %rsi, %rcx
    call Lworker_registry_reap_terminal
    movq %rsi, %rcx
    call vpp_native_worker_cancelled
    testl %eax, %eax
    jnz Lworker_service_cancelled
    cmpl $1, %edi
    jne Lworker_service_failed
    cmpl $0, 40(%rsi)
    jl Lworker_service_failed
    movl $1, %eax
    jmp Lworker_service_return
Lworker_service_cancel:
    call vpp_native_worker_request_cancel
    jmp Lworker_service_return
Lworker_service_cancelled:
    movl $2, %eax
    jmp Lworker_service_return
Lworker_service_failed:
    movl $3, %eax
    jmp Lworker_service_return
Lworker_service_self:
    call vpp_native_worker_current
    testq %rax, %rax
    jz Lworker_service_self_root
    movq %rax, %rcx
    cmpl $5, %ebx
    je Lworker_service_current_cancel
    call vpp_native_worker_registry_self
    jmp Lworker_service_self_handle
Lworker_service_current_cancel:
    call vpp_native_worker_cancelled
    jmp Lworker_service_return
Lworker_service_self_root:
    xorl %eax, %eax
Lworker_service_self_handle:
    cmpl $6, %ebx
    je Lworker_service_wait_begin
    cmpl $7, %ebx
    je Lworker_service_wait_end
    jmp Lworker_service_return
Lworker_service_wait_begin:
    movl %eax, %ecx
    movl %esi, %edx
    call vpp_native_worker_registry_wait_begin
    jmp Lworker_service_return
Lworker_service_wait_end:
    movl %eax, %ecx
    call vpp_native_worker_registry_wait_end
    xorl %eax, %eax
    jmp Lworker_service_return
Lworker_service_output:
    cmpl $9, %ebx
    je Lworker_service_copy
    movl %esi, %ecx
    call vpp_native_worker_registry_record
    testq %rax, %rax
    jz Lworker_service_invalid
    movq %rax, %rcx
    leaq 32(%rsp), %rdx
    call vpp_native_worker_result
    testl %eax, %eax
    jnz Lworker_service_invalid
    movl 32(%rsp), %eax
    jmp Lworker_service_return
Lworker_service_copy:
    cmpl $(VPP_ARENA_SIZE - 12), %edi
    ja Lworker_service_invalid
    call vpp_native_memory_base
    testq %rax, %rax
    jz Lworker_service_invalid
    movl (%rax,%rdi), %edx
    movl 4(%rax,%rdi), %r8d
    movl 8(%rax,%rdi), %r9d
    movl %esi, %ecx
    call vpp_native_worker_registry_copy_result
    jmp Lworker_service_return
Lworker_service_root:
    call vpp_native_worker_current
    testq %rax, %rax
    jnz Lworker_service_invalid
    cmpl $11, %ebx
    je Lworker_service_open
    call vpp_native_worker_registry_shutdown
    jmp Lworker_service_return
Lworker_service_open:
    call vpp_native_worker_registry_open
    jmp Lworker_service_return
#ifdef VPP_STANDALONE_VM_WORKER
Lworker_service_vm_register:
    call vpp_native_worker_current
    testq %rax, %rax
    jnz Lworker_service_invalid
    call vpp_native_worker_registry_open
    testl %eax, %eax
    jnz Lworker_service_return
    leaq Lworker_vm_entry_callback(%rip), %rcx
    call vpp_native_worker_set_entry
    jmp Lworker_service_return
Lworker_vm_entry_callback:
    subq $56, %rsp
    movq %rcx, 32(%rsp)
    movl %edx, %ecx
    call vpp_fn_vm_worker_entry
    movl %eax, %edx
    movq 32(%rsp), %rcx
    addq $56, %rsp
    jmp vpp_native_worker_compact_result
#endif
Lworker_service_invalid:
    movl $-1, %eax
Lworker_service_return:
    addq $48, %rsp
    popq %rdi
    popq %rsi
    popq %rbx
    ret
