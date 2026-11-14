// Shared SysV/Win64 in-memory program boundary. The V++ image writer owns
// all machine instructions and metadata; this file only marshals the ABI.
.text
.p2align 4
.Ljit_x86_active:
    xorl %eax, %eax
    cmpq $0, vpp_jit_active_spec(%rip)
    setne %al
    ret
.Ljit_x86_address:
    pushq %rbx
    pushq %r12
    pushq %r13
    subq $32, %rsp
#ifdef _WIN32
    movl %edx, %ebx
    movl %r8d, %r12d
    movl %r12d, %ecx
#else
    movl %esi, %ebx
    movl %edx, %r12d
    movl %r12d, %edi
#endif
    cmpl $(.Ljit_x86_table_end-.Ljit_x86_table)/8, %ebx
    jae .Ljit_x86_address_bad
    call vpp_native_bytes_length
    cmpl $8, %eax
    jl .Ljit_x86_address_bad
    leaq .Ljit_x86_table(%rip), %rax
    movq (%rax,%rbx,8), %r13
#ifdef _WIN32
    movl %r12d, %ecx
    xorl %edx, %edx
    movl %r13d, %r8d
#else
    movl %r12d, %edi
    xorl %esi, %esi
    movl %r13d, %edx
#endif
    call vpp_native_bytes_store_i32
    testl %eax, %eax
    jne .Ljit_x86_address_bad
    shrq $32, %r13
#ifdef _WIN32
    movl %r12d, %ecx
    movl $4, %edx
    movl %r13d, %r8d
#else
    movl %r12d, %edi
    movl $4, %esi
    movl %r13d, %edx
#endif
    call vpp_native_bytes_store_i32
    jmp .Ljit_x86_address_done
.Ljit_x86_address_bad:
    movl $-1, %eax
.Ljit_x86_address_done:
    addq $32, %rsp
    popq %r13
    popq %r12
    popq %rbx
    ret

.Ljit_x86_run:
    pushq %rbx
    pushq %r12
    pushq %r13
    subq $32, %rsp
    cmpq $0, vpp_jit_active_spec(%rip)
    jne .Ljit_x86_run_bad
#ifdef _WIN32
    movl %r8d, %ebx
    movl %edx, %ecx
#else
    movl %edx, %ebx
    movl %esi, %edi
#endif
    movl %ebx, %r13d
    shrl $24, %r13d
    andl $0xffffff, %ebx
    call vpp_native_jit_image
    testq %rax, %rax
    je .Ljit_x86_run_bad
    testl $7, %ebx
    jne .Ljit_x86_run_bad
    cmpl $96, %edx
    jb .Ljit_x86_run_bad
    subl $96, %edx
    cmpl %edx, %ebx
    ja .Ljit_x86_run_bad
    addq %rax, %rbx
    call vpp_native_argc
    testl %eax, %eax
    jle .Ljit_x86_run_bad
    cmpl %eax, %r13d
    jae .Ljit_x86_run_bad
    movl %r13d, vpp_jit_argv_offset(%rip)
    movq %rbx, vpp_jit_active_spec(%rip)
#ifdef _WIN32
    xorl %ecx, %ecx
#else
    xorl %edi, %edi
#endif
    call vpp_native_jit_swap_arena
    movq %rax, %r12
    call vpp_native_memory_base
    movl $-2, %r13d
    testq %rax, %rax
    je .Ljit_x86_restore
    call main
    movl %eax, %r13d
.Ljit_x86_restore:
#ifdef _WIN32
    movq %r12, %rcx
#else
    movq %r12, %rdi
#endif
    call vpp_native_jit_swap_arena
    testq %rax, %rax
    je .Ljit_x86_restored
#ifdef _WIN32
    movq %rax, %rcx
    xorl %edx, %edx
    movl $0x8000, %r8d
    call VirtualFree
#else
    movq %rax, %rdi
    movl $135271424, %esi
    movl $11, %eax
    syscall
#endif
.Ljit_x86_restored:
    movl $0, vpp_jit_argv_offset(%rip)
    movq $0, vpp_jit_active_spec(%rip)
    movl %r13d, %eax
    jmp .Ljit_x86_run_done
.Ljit_x86_run_bad:
    movl $-2, %eax
.Ljit_x86_run_done:
    addq $32, %rsp
    popq %r13
    popq %r12
    popq %rbx
    ret

#ifdef _WIN32
.section .rdata,"dr"
#else
.section .data.rel.ro,"aw",@progbits
#endif
.p2align 3
.Ljit_x86_table:
.purgem AOT_VPP
.purgem AOT_VPP_GAP_I32
.macro AOT_VPP ten, so_doi_so
    .quad vpp_native_dynamic_\ten
.endm
.macro AOT_VPP_GAP_I32 ten, so_doi_so
    .quad vpp_native_dynamic_\ten
.endm
#include "vpp_aot_vpp_exports.inc"
    .quad vpp_native_i32_div
    .quad vpp_native_print_i32
.Ljit_x86_table_end:
.bss
.p2align 3
vpp_jit_active_spec: .zero 8
.text
