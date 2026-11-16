// SysV AMD64 worker registry. Same 64-byte public record protocol as ARM64/Windows.
// Linux appends private stack/TID/TLS fields; guest values remain handles/offsets.
#include "worker_os.inc"
#include "worker_transport.inc"

// Root-owned worker registry, same protocol as the ARM64 implementation.
// All pointers remain in native storage, and all records outlive waiters.
#ifdef __APPLE__
.section __DATA,__bss
#else
.bss
#endif
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
    cmpl $1, %edi
    jl 1f
    cmpl Lworker_registry_count(%rip), %edi
    jg 1f
    decl %edi
    leaq Lworker_registry_slots(%rip), %rax
    movq (%rax,%rdi,8), %rax
    ret
1:  xorl %eax, %eax
    ret

.globl PUBLIC(vpp_native_worker_registry_start)
PUBLIC(vpp_native_worker_registry_start):
    pushq %rbx
    pushq %r12
    pushq %r13
    subq $32, %rsp
    movq %rdi, %r12
    movq %rsi, %r13
    call Lworker_registry_lock
    cmpl $0, Lworker_registry_closing(%rip)
    jne Lworker_registry_start_fail
    movl Lworker_registry_count(%rip), %ebx
    cmpl $4096, %ebx
    jge Lworker_registry_start_fail
    movq %r12, %rdi
    movq %r13, %rsi
    call PUBLIC(vpp_native_worker_start)
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
    popq %r13
    popq %r12
    popq %rbx
    ret

.globl PUBLIC(vpp_native_worker_registry_record)
PUBLIC(vpp_native_worker_registry_record):
    subq $40, %rsp
    call Lworker_registry_lock
    call Lworker_registry_find
    movq %rax, %r10
    call Lworker_registry_unlock
    movq %r10, %rax
    addq $40, %rsp
    ret

.globl PUBLIC(vpp_native_worker_registry_self)
PUBLIC(vpp_native_worker_registry_self):
    subq $40, %rsp
    call Lworker_registry_lock
    movl 48(%rdi), %r10d
    call Lworker_registry_unlock
    movl %r10d, %eax
    addq $40, %rsp
    ret

.globl PUBLIC(vpp_native_worker_registry_wait_begin)
PUBLIC(vpp_native_worker_registry_wait_begin):
    pushq %rbx
    pushq %r12
    pushq %r13
    subq $48, %rsp
    movl %edi, %ebx
    movl %esi, %r12d
    call Lworker_registry_lock
    movl %r12d, %edi
    call Lworker_registry_find
    testq %rax, %rax
    jz Lworker_registry_wait_invalid
    movq %rax, %r13
    testl %ebx, %ebx
    jz Lworker_registry_wait_ok
    cmpl %r12d, %ebx
    je Lworker_registry_wait_cycle
    movl %ebx, %edi
    call Lworker_registry_find
    testq %rax, %rax
    jz Lworker_registry_wait_invalid
    movq %rax, 32(%rsp)
    cmpl $0, 52(%rax)
    jne Lworker_registry_wait_cycle
Lworker_registry_wait_walk:
    movl 52(%r13), %edi
    testl %edi, %edi
    jz Lworker_registry_wait_commit
    cmpl %ebx, %edi
    je Lworker_registry_wait_cycle
    call Lworker_registry_find
    testq %rax, %rax
    jz Lworker_registry_wait_invalid
    movq %rax, %r13
    jmp Lworker_registry_wait_walk
Lworker_registry_wait_commit:
    movq 32(%rsp), %rax
    movl %r12d, 52(%rax)
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
    popq %r13
    popq %r12
    popq %rbx
    ret

.globl PUBLIC(vpp_native_worker_registry_wait_end)
PUBLIC(vpp_native_worker_registry_wait_end):
    testl %edi, %edi
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

.globl PUBLIC(vpp_native_worker_registry_shutdown)
PUBLIC(vpp_native_worker_registry_shutdown):
    pushq %rbx
    pushq %r12
    pushq %r13
    subq $32, %rsp
    call Lworker_registry_lock
    movl $1, Lworker_registry_closing(%rip)
    movl Lworker_registry_count(%rip), %ebx
    leaq Lworker_registry_slots(%rip), %r13
    xorl %r12d, %r12d
Lworker_registry_shutdown_cancel:
    cmpl %ebx, %r12d
    jge Lworker_registry_shutdown_unlock
    movq (%r13,%r12,8), %rdi
    testq %rdi, %rdi
    jz 1f
    call PUBLIC(vpp_native_worker_request_cancel)
1:  incl %r12d
    jmp Lworker_registry_shutdown_cancel
Lworker_registry_shutdown_unlock:
    call Lworker_registry_unlock
    xorl %r12d, %r12d
Lworker_registry_shutdown_join:
    cmpl %ebx, %r12d
    jge Lworker_registry_shutdown_dispose_begin
    movq (%r13,%r12,8), %rdi
    testq %rdi, %rdi
    jz 1f
    call PUBLIC(vpp_native_worker_join)
    testl %eax, %eax
    jnz Lworker_registry_shutdown_fail
1:  incl %r12d
    jmp Lworker_registry_shutdown_join
Lworker_registry_shutdown_dispose_begin:
    xorl %r12d, %r12d
Lworker_registry_shutdown_dispose:
    cmpl %ebx, %r12d
    jge Lworker_registry_shutdown_done
    movq (%r13,%r12,8), %rdi
    testq %rdi, %rdi
    jz 1f
    call PUBLIC(vpp_native_worker_dispose)
    testl %eax, %eax
    jnz Lworker_registry_shutdown_fail
    movq $0, (%r13,%r12,8)
1:  incl %r12d
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
    popq %r13
    popq %r12
    popq %rbx
    ret

.globl PUBLIC(vpp_native_worker_registry_open)
PUBLIC(vpp_native_worker_registry_open):
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
.globl PUBLIC(vpp_native_worker_registry_start_copy)
PUBLIC(vpp_native_worker_registry_start_copy):
    testq %rdi, %rdi
    jz Lworker_copy_invalid
    cmpl $VPP_ARENA_SIZE, %edx
    ja Lworker_copy_invalid
    movl $VPP_ARENA_SIZE, %eax
    subl %edx, %eax
    cmpl %eax, %esi
    ja Lworker_copy_invalid
    cmpl %eax, %ecx
    ja Lworker_copy_invalid
    pushq %rbx
    subq $64, %rsp
    movq %rdi, 32(%rsp)
    movl %esi, %ebx
    movl %edx, 48(%rsp)
    movl %ecx, 52(%rsp)
    call PUBLIC(vpp_native_memory_base)
    testq %rax, %rax
    jz Lworker_start_copy_fail
    addq %rbx, %rax
    movq %rax, 40(%rsp)
    leaq Lworker_copy_entry(%rip), %rdi
    leaq 32(%rsp), %rsi
    call PUBLIC(vpp_native_worker_registry_start)
    testl %eax, %eax
    jle Lworker_start_copy_fail
    movl %eax, %ebx
    movl %eax, %edi
    call PUBLIC(vpp_native_worker_registry_record)
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
    pushq %r12
    pushq %r13
    subq $48, %rsp
    movq %rdi, %rbx
    movq (%rsi), %rax
    movq %rax, 32(%rsp)
    movq 8(%rsi), %r12
    movl 16(%rsi), %edi
    movl 20(%rsi), %eax
    movl %eax, 40(%rsp)
    movq 24(%rbx), %r13
    addq %rax, %r13
    movl %edi, %ecx
    movq %r12, %rsi
    movq %r13, %rdi
    cld
    rep movsb
    movq $0, 8(%rbx)
    movl $1, 56(%rbx)
    movq %rbx, %rdi
    movl 40(%rsp), %esi
    call *32(%rsp)
    addq $48, %rsp
    popq %r13
    popq %r12
    popq %rbx
    ret

.globl PUBLIC(vpp_native_worker_registry_copy_result)
PUBLIC(vpp_native_worker_registry_copy_result):
    cmpl $VPP_ARENA_SIZE, %edx
    ja Lworker_copy_invalid
    movl $VPP_ARENA_SIZE, %eax
    subl %edx, %eax
    cmpl %eax, %esi
    ja Lworker_copy_invalid
    cmpl %eax, %ecx
    ja Lworker_copy_invalid
    pushq %rbx
    pushq %r12
    pushq %r13
    subq $48, %rsp
    movl %esi, %r12d
    movl %edx, %ebx
    movl %ecx, %r13d
    call PUBLIC(vpp_native_worker_registry_record)
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
2:  subl %eax, %r12d
    jb Lworker_copy_result_fail
    subl %ebx, %r11d
    jb Lworker_copy_result_fail
    cmpl %r11d, %r12d
    ja Lworker_copy_result_fail
1:  addq %r10, %r12
    call PUBLIC(vpp_native_memory_base)
    testq %rax, %rax
    jz Lworker_copy_result_fail
    addq %rax, %r13
    movl %ebx, %edi
    movl %edi, %ecx
    movq %r12, %rsi
    movq %r13, %rdi
    cld
    rep movsb
    xorl %eax, %eax
    jmp Lworker_copy_result_return
Lworker_copy_result_fail:
    movl $-1, %eax
Lworker_copy_result_return:
    addq $48, %rsp
    popq %r13
    popq %r12
    popq %rbx
    ret

 .globl PUBLIC(vpp_native_worker_current)
PUBLIC(vpp_native_worker_current):
    cmpl $0, PUBLIC(vpp_native_arena_tls_enabled)(%rip)
    je 1f
    jmp Lworker_os_current
1:  xorl %eax, %eax
    ret

// Serialize terminal reaping with other observers and root shutdown.
Lworker_registry_reap_terminal:
    subq $56, %rsp
    movq %rdi, 32(%rsp)
    call Lworker_registry_lock
    cmpl $0, Lworker_registry_closing(%rip)
    jne 1f
    movq 32(%rsp), %rdi
    call PUBLIC(vpp_native_worker_join)
1:  call Lworker_registry_unlock
    addq $56, %rsp
    ret

// Pointer-safe worker service. The root binds the native callback outside
// the guest ABI; guest integers contain only handles or arena offsets.
#ifdef __APPLE__
.section __DATA,__bss
#else
.bss
#endif
.p2align 3
Lworker_service_entry: .quad 0
.text
.globl PUBLIC(vpp_native_worker_set_entry)
PUBLIC(vpp_native_worker_set_entry):
    subq $56, %rsp
    movq %rdi, 32(%rsp)
    call PUBLIC(vpp_native_worker_current)
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
.globl PUBLIC(vpp_native_worker_service)
PUBLIC(vpp_native_worker_service):
    pushq %rbx
    pushq %r12
    pushq %r13
    subq $48, %rsp
    movl %edi, %ebx
    movl %esi, %r12d
    movl %edx, %r13d
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
    cmpl $31000000, %r13d
    ja Lworker_service_invalid
    movq Lworker_service_entry(%rip), %rdi
    testq %rdi, %rdi
    jz Lworker_service_invalid
    movl %r12d, %esi
    movl %r13d, %edx
    movl $89000000, %ecx
    call PUBLIC(vpp_native_worker_registry_start_copy)
    jmp Lworker_service_return
Lworker_service_lookup:
    movl %r12d, %edi
    call PUBLIC(vpp_native_worker_registry_record)
    testq %rax, %rax
    jz Lworker_service_invalid
    movq %rax, %rdi
    cmpl $3, %ebx
    je Lworker_service_cancel
    movq %rax, %r12
    call PUBLIC(vpp_native_worker_state)
    testl %eax, %eax
    jz Lworker_service_return
    movl %eax, %r13d
    movq %r12, %rdi
    call Lworker_registry_reap_terminal
    movq %r12, %rdi
    call PUBLIC(vpp_native_worker_cancelled)
    testl %eax, %eax
    jnz Lworker_service_cancelled
    cmpl $1, %r13d
    jne Lworker_service_failed
    cmpl $0, 40(%r12)
    jl Lworker_service_failed
    movl $1, %eax
    jmp Lworker_service_return
Lworker_service_cancel:
    call PUBLIC(vpp_native_worker_request_cancel)
    jmp Lworker_service_return
Lworker_service_cancelled:
    movl $2, %eax
    jmp Lworker_service_return
Lworker_service_failed:
    movl $3, %eax
    jmp Lworker_service_return
Lworker_service_self:
    call PUBLIC(vpp_native_worker_current)
    testq %rax, %rax
    jz Lworker_service_self_root
    movq %rax, %rdi
    cmpl $5, %ebx
    je Lworker_service_current_cancel
    call PUBLIC(vpp_native_worker_registry_self)
    jmp Lworker_service_self_handle
Lworker_service_current_cancel:
    call PUBLIC(vpp_native_worker_cancelled)
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
    movl %eax, %edi
    movl %r12d, %esi
    call PUBLIC(vpp_native_worker_registry_wait_begin)
    jmp Lworker_service_return
Lworker_service_wait_end:
    movl %eax, %edi
    call PUBLIC(vpp_native_worker_registry_wait_end)
    xorl %eax, %eax
    jmp Lworker_service_return
Lworker_service_output:
    cmpl $9, %ebx
    je Lworker_service_copy
    movl %r12d, %edi
    call PUBLIC(vpp_native_worker_registry_record)
    testq %rax, %rax
    jz Lworker_service_invalid
    movq %rax, %rdi
    leaq 32(%rsp), %rsi
    call PUBLIC(vpp_native_worker_result)
    testl %eax, %eax
    jnz Lworker_service_invalid
    movl 32(%rsp), %eax
    jmp Lworker_service_return
Lworker_service_copy:
    cmpl $(VPP_ARENA_SIZE - 12), %r13d
    ja Lworker_service_invalid
    call PUBLIC(vpp_native_memory_base)
    testq %rax, %rax
    jz Lworker_service_invalid
    movl (%rax,%r13), %esi
    movl 4(%rax,%r13), %edx
    movl 8(%rax,%r13), %ecx
    movl %r12d, %edi
    call PUBLIC(vpp_native_worker_registry_copy_result)
    jmp Lworker_service_return
Lworker_service_root:
    call PUBLIC(vpp_native_worker_current)
    testq %rax, %rax
    jnz Lworker_service_invalid
    cmpl $11, %ebx
    je Lworker_service_open
    call PUBLIC(vpp_native_worker_registry_shutdown)
    jmp Lworker_service_return
Lworker_service_open:
    call PUBLIC(vpp_native_worker_registry_open)
    jmp Lworker_service_return
#ifdef VPP_STANDALONE_VM_WORKER
Lworker_service_vm_register:
    call PUBLIC(vpp_native_worker_current)
    testq %rax, %rax
    jnz Lworker_service_invalid
    call PUBLIC(vpp_native_worker_registry_open)
    testl %eax, %eax
    jnz Lworker_service_return
    leaq Lworker_vm_entry_callback(%rip), %rdi
    call PUBLIC(vpp_native_worker_set_entry)
    jmp Lworker_service_return
Lworker_vm_entry_callback:
    subq $56, %rsp
    movq %rdi, 32(%rsp)
#if defined(__APPLE__) && VPP_NATIVE_RUNTIME_STACK_ABI
    movl %esi, (%rsp)
#else
    movl %esi, %edi
#endif
    call PUBLIC(vpp_fn_vm_worker_entry)
    movl %eax, %esi
    movq 32(%rsp), %rdi
    addq $56, %rsp
    jmp PUBLIC(vpp_native_worker_compact_result)
#endif
Lworker_service_invalid:
    movl $-1, %eax
Lworker_service_return:
    addq $48, %rsp
    popq %r13
    popq %r12
    popq %rbx
    ret
