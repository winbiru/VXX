// Same handle/byte-packet contract as the ARM64 boundary. No host pointers
// are truncated to i32. V++ emits and relocates all published instructions.
// Operation 7 runs a standalone zero-arity entry in a fresh arena.
// Operation 6 invokes a two-i32 leaf using 12 bytes of current arena scratch.
.text
.p2align 4
.macro JIT_ARG1 value
#ifdef _WIN32
    movl \value, %ecx
#else
    movl \value, %edi
#endif
.endm
.macro JIT_ARG2 value
#ifdef _WIN32
    movl \value, %edx
#else
    movl \value, %esi
#endif
.endm
.macro JIT_ARG3 value
#ifdef _WIN32
    movl \value, %r8d
#else
    movl \value, %edx
#endif
.endm
.globl vpp_native_jit_service
vpp_native_jit_service:
    pushq %rbp
    movq %rsp, %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    pushq %rdi
    pushq %rsi
    subq $216, %rsp
#ifdef _WIN32
    movl %ecx, -64(%rbp)
    movl %edx, %r12d
    movl %r8d, %r13d
#else
    movl %edi, -64(%rbp)
    movl %esi, %r12d
    movl %edx, %r13d
#endif
    cmpl $10, -64(%rbp)
    je .Lvpp_jit_staging_new
    cmpl $11, -64(%rbp)
    jl 1f
    cmpl $16, -64(%rbp)
    jle .Lvpp_jit_lookup
1:  cmpl $0, -64(%rbp)
    je .Lvpp_jit_host
    cmpl $1, -64(%rbp)
    je .Lvpp_jit_publish
    cmpl $5, -64(%rbp)
    je .Lvpp_jit_publish
    cmpl $4, -64(%rbp)
    je .Lvpp_jit_address
    cmpl $7, -64(%rbp)
    je .Lvpp_jit_lookup
    cmpl $6, -64(%rbp)
    je .Lvpp_jit_lookup
    cmpl $2, -64(%rbp)
    jl .Lvpp_jit_invalid
    cmpl $3, -64(%rbp)
    jg .Lvpp_jit_invalid
.Lvpp_jit_lookup:
    testl %r12d, %r12d
    jle .Lvpp_jit_invalid
    leaq vpp_jit_slots(%rip), %rbx
    movl $64, %eax
.Lvpp_jit_find:
    cmpl %r12d, 16(%rbx)
    je .Lvpp_jit_found
    addq $32, %rbx
    decl %eax
    jne .Lvpp_jit_find
    jmp .Lvpp_jit_invalid
.Lvpp_jit_found:
    cmpl $3, -64(%rbp)
    je .Lvpp_jit_release
    cmpl $10, 20(%rbx)
    je .Lvpp_jit_staging
    cmpl $10, -64(%rbp)
    jge .Lvpp_jit_invalid
    cmpl $1, 20(%rbx)
    jne .Lvpp_jit_invalid
    cmpl $6, -64(%rbp)
    je .Lvpp_jit_leaf
    JIT_ARG1 %r13d
    call vpp_native_bytes_length
    cmpl $44, %eax
    jl .Lvpp_jit_invalid
    JIT_ARG1 %r13d
    JIT_ARG2 $0
    call vpp_native_bytes_load_i32
    testl %eax, %eax
    js .Lvpp_jit_invalid
    cmpl 8(%rbx), %eax
    jae .Lvpp_jit_invalid
    addq (%rbx), %rax
    movq %rax, -104(%rbp)
    JIT_ARG1 %r13d
    JIT_ARG2 $4
    call vpp_native_bytes_load_i32
    cmpl $7, -64(%rbp)
    je .Lvpp_jit_program
    cmpl $8, %eax
    ja .Lvpp_jit_invalid
    xorl %r14d, %r14d
.Lvpp_jit_arguments:
    JIT_ARG1 %r13d
    leal 8(,%r14,4), %eax
    JIT_ARG2 %eax
    call vpp_native_bytes_load_i32
    movl %eax, -192(%rbp,%r14,4)
    incl %r14d
    cmpl $8, %r14d
    jne .Lvpp_jit_arguments
#ifdef _WIN32
    movslq -176(%rbp), %rax
    movq %rax, 32(%rsp)
    movslq -172(%rbp), %rax
    movq %rax, 40(%rsp)
    movslq -168(%rbp), %rax
    movq %rax, 48(%rsp)
    movslq -164(%rbp), %rax
    movq %rax, 56(%rsp)
    movl -192(%rbp), %ecx
    movl -188(%rbp), %edx
    movl -184(%rbp), %r8d
    movl -180(%rbp), %r9d
#else
    movslq -168(%rbp), %rax
    movq %rax, (%rsp)
    movslq -164(%rbp), %rax
    movq %rax, 8(%rsp)
    movl -192(%rbp), %edi
    movl -188(%rbp), %esi
    movl -184(%rbp), %edx
    movl -180(%rbp), %ecx
    movl -176(%rbp), %r8d
    movl -172(%rbp), %r9d
#endif
    call *-104(%rbp)
    JIT_ARG3 %eax
    JIT_ARG1 %r13d
    JIT_ARG2 $40
    call vpp_native_bytes_store_i32
    jmp .Lvpp_jit_done
.Lvpp_jit_program:
#if defined(VPP_WINDOWS_STAGE0)
    jmp .Lvpp_jit_invalid
#else
    testl %eax, %eax
    jne .Lvpp_jit_invalid
    cmpl $0, vpp_jit_argv_offset(%rip)
    jne .Lvpp_jit_invalid
    JIT_ARG1 %r13d
    call vpp_native_bytes_length
    cmpl $48, %eax
    jl .Lvpp_jit_invalid
    JIT_ARG1 %r13d
    JIT_ARG2 $44
    call vpp_native_bytes_load_i32
    movl %eax, %r14d
    testl %r14d, %r14d
    jle .Lvpp_jit_invalid
    call vpp_native_argc
    testl %eax, %eax
    jle .Lvpp_jit_invalid
    cmpl %eax, %r14d
    jae .Lvpp_jit_invalid
    movl %r14d, vpp_jit_argv_offset(%rip)
    JIT_ARG1 $0
    call vpp_native_jit_swap_arena
    movq %rax, %r12
    call vpp_native_memory_base
    testq %rax, %rax
    je .Lvpp_jit_program_alloc_failed
    movq %rax, %r14
    call *-104(%rbp)
    movl %eax, %r15d
    addq $2064, %r14
    movl $64, %ebx
.Lvpp_jit_program_close_files:
    movl (%r14), %eax
    testl %eax, %eax
    je 1f
    JIT_ARG1 %eax
    call vpp_native_file_close
1:  addq $16, %r14
    decl %ebx
    jne .Lvpp_jit_program_close_files
#ifdef _WIN32
    movq %r12, %rcx
#else
    movq %r12, %rdi
#endif
    call vpp_native_jit_swap_arena
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
    movl $0, vpp_jit_argv_offset(%rip)
    JIT_ARG3 %r15d
    JIT_ARG1 %r13d
    JIT_ARG2 $40
    call vpp_native_bytes_store_i32
    jmp .Lvpp_jit_done
.Lvpp_jit_program_alloc_failed:
#ifdef _WIN32
    movq %r12, %rcx
#else
    movq %r12, %rdi
#endif
    call vpp_native_jit_swap_arena
    movl $0, vpp_jit_argv_offset(%rip)
    jmp .Lvpp_jit_resource
#endif
.Lvpp_jit_leaf:
    testl $3, %r13d
    jne .Lvpp_jit_invalid
#ifdef VPP_LINUX_ARENA_SIZE
    cmpl $(VPP_LINUX_ARENA_SIZE - 12), %r13d
#else
    cmpl $(VPP_ARENA_SIZE - 12), %r13d
#endif
    ja .Lvpp_jit_invalid
    movq vpp_native_arena_base(%rip), %r15
    testq %r15, %r15
    je .Lvpp_jit_invalid
    movl %r13d, %eax
    addq %rax, %r15
#ifdef _WIN32
    movl (%r15), %ecx
    movl 4(%r15), %edx
#else
    movl (%r15), %edi
    movl 4(%r15), %esi
#endif
    call *(%rbx)
    movl %eax, 8(%r15)
    xorl %eax, %eax
    jmp .Lvpp_jit_done
.Lvpp_jit_host:
#ifdef _WIN32
    movl $3, %eax
#else
    movl $2, %eax
#endif
    jmp .Lvpp_jit_done
.Lvpp_jit_publish:
    JIT_ARG1 %r12d
    call vpp_native_bytes_length
    testl %eax, %eax
    jle .Lvpp_jit_invalid
    cmpl $5, -64(%rbp)
    je .Lvpp_jit_dynamic_limit
    cmpl $1048576, %eax
    jg .Lvpp_jit_invalid
    jmp .Lvpp_jit_limit_done
.Lvpp_jit_dynamic_limit:
    cmpl $2097152, %eax
    jg .Lvpp_jit_invalid
.Lvpp_jit_limit_done:
    movl %eax, %r14d
    movl %eax, -84(%rbp)
    cmpl $5, -64(%rbp)
    jne .Lvpp_jit_size_ready
    testl %r13d, %r13d
    jle .Lvpp_jit_invalid
    cmpl %r14d, %r13d
    ja .Lvpp_jit_invalid
    subl %r13d, %eax
    testl $3, %eax
    jne .Lvpp_jit_invalid
    movl %r13d, %r14d
.Lvpp_jit_staging_new:
    // Capacity is independent of the four public byte handles.
    cmpl $10, -64(%rbp)
    jne .Lvpp_jit_size_ready
    testl %r12d, %r12d
    jle .Lvpp_jit_invalid
    cmpl $16777216, %r12d
    ja .Lvpp_jit_invalid
    movl %r12d, %r14d
.Lvpp_jit_size_ready:
    cmpl $0x7fffffff, vpp_jit_next(%rip)
    je .Lvpp_jit_resource
    leaq vpp_jit_slots(%rip), %rbx
    movl $64, %eax
.Lvpp_jit_empty:
    cmpq $0, (%rbx)
    je .Lvpp_jit_allocate
    addq $32, %rbx
    decl %eax
    jne .Lvpp_jit_empty
    jmp .Lvpp_jit_resource
.Lvpp_jit_allocate:
#ifdef _WIN32
    xorl %ecx, %ecx
    movl %r14d, %edx
    movl $0x3000, %r8d
    movl $4, %r9d
    call VirtualAlloc
    testq %rax, %rax
    je .Lvpp_jit_resource
#else
    xorl %edi, %edi
    movl %r14d, %esi
    movl $3, %edx
    movl $0x22, %r10d
    movq $-1, %r8
    xorl %r9d, %r9d
    movl $9, %eax
    syscall
    cmpq $-4095, %rax
    jae .Lvpp_jit_resource
#endif
    movq %rax, %r15
    cmpl $10, -64(%rbp)
    jne 1f
    incl vpp_jit_next(%rip)
    movl vpp_jit_next(%rip), %eax
    movq %r15, (%rbx)
    movl %r14d, 8(%rbx)
    movl $0, 12(%rbx)
    movl %eax, 16(%rbx)
    movl $10, 20(%rbx)
    movl %r14d, 24(%rbx)
    jmp .Lvpp_jit_done
1:  movl $0, -80(%rbp)
.Lvpp_jit_copy:
    JIT_ARG1 %r12d
    JIT_ARG2 -80(%rbp)
    call vpp_native_bytes_get
    cmpl $255, %eax
    ja .Lvpp_jit_rollback
    movl -80(%rbp), %ecx
    movb %al, (%r15,%rcx)
    incl %ecx
    movl %ecx, -80(%rbp)
    cmpl %r14d, %ecx
    jne .Lvpp_jit_copy
    cmpl $5, -64(%rbp)
    jne .Lvpp_jit_relocated
.Lvpp_jit_relocate:
    movl -80(%rbp), %eax
    cmpl -84(%rbp), %eax
    jae .Lvpp_jit_relocated
    JIT_ARG1 %r12d
    JIT_ARG2 %eax
    call vpp_native_bytes_load_i32
    testl %eax, %eax
    js .Lvpp_jit_rollback
    testl $7, %eax
    jne .Lvpp_jit_rollback
    cmpl $8, %r14d
    jb .Lvpp_jit_rollback
    leal -8(%r14), %ecx
    cmpl %ecx, %eax
    ja .Lvpp_jit_rollback
    movq (%r15,%rax), %rcx
    cmpq %r14, %rcx
    jae .Lvpp_jit_rollback
    addq %r15, %rcx
    movq %rcx, (%r15,%rax)
    addl $4, -80(%rbp)
    jmp .Lvpp_jit_relocate
.Lvpp_jit_relocated:
#ifdef _WIN32
    movq %r15, %rcx
    movl %r14d, %edx
    movl $0x20, %r8d
    leaq -120(%rbp), %r9
    call VirtualProtect
    testl %eax, %eax
    je .Lvpp_jit_rollback
    movq $-1, %rcx
    movq %r15, %rdx
    movl %r14d, %r8d
    call FlushInstructionCache
    testl %eax, %eax
    je .Lvpp_jit_rollback
#else
    movq %r15, %rdi
    movl %r14d, %esi
    movl $5, %edx
    movl $10, %eax
    syscall
    testq %rax, %rax
    jne .Lvpp_jit_rollback
#endif
    incl vpp_jit_next(%rip)
    movl vpp_jit_next(%rip), %eax
    movq %r15, (%rbx)
    movl %r14d, 8(%rbx)
    movl %eax, 16(%rbx)
    movl -64(%rbp), %ecx
    movl %ecx, 20(%rbx)
    movl $0, 24(%rbx)
    jmp .Lvpp_jit_done
.Lvpp_jit_rollback:
#ifdef _WIN32
    movq %r15, %rcx
    xorl %edx, %edx
    movl $0x8000, %r8d
    call VirtualFree
#else
    movq %r15, %rdi
    movl %r14d, %esi
    movl $11, %eax
    syscall
#endif
    jmp .Lvpp_jit_resource
// 11 append word, 12 append byte, 13 read byte, 14 read word,
// 15 patch word from byte packet [offset,value], 16 seal(image size).
.Lvpp_jit_staging:
    movq (%rbx), %r15
    movl 12(%rbx), %r14d
    cmpl $11, -64(%rbp)
    je .Lvpp_jit_stage_word
    cmpl $12, -64(%rbp)
    je .Lvpp_jit_stage_byte
    cmpl $13, -64(%rbp)
    je .Lvpp_jit_stage_read_byte
    cmpl $14, -64(%rbp)
    je .Lvpp_jit_stage_read_word
    cmpl $15, -64(%rbp)
    je .Lvpp_jit_stage_patch
    cmpl $16, -64(%rbp)
    je .Lvpp_jit_stage_seal
    jmp .Lvpp_jit_invalid
.Lvpp_jit_stage_word:
    leal 4(%r14), %eax
    cmpl 8(%rbx), %eax
    ja .Lvpp_jit_invalid
    movl %r13d, (%r15,%r14)
    movl %eax, 12(%rbx)
    jmp .Lvpp_jit_stage_ok
.Lvpp_jit_stage_byte:
    cmpl $255, %r13d
    ja .Lvpp_jit_invalid
    cmpl 8(%rbx), %r14d
    jae .Lvpp_jit_invalid
    movb %r13b, (%r15,%r14)
    incl 12(%rbx)
    jmp .Lvpp_jit_stage_ok
.Lvpp_jit_stage_read_byte:
    cmpl %r14d, %r13d
    jae .Lvpp_jit_invalid
    movzbl (%r15,%r13), %eax
    jmp .Lvpp_jit_done
.Lvpp_jit_stage_read_word:
    cmpl $4, %r14d
    jb .Lvpp_jit_invalid
    leal -4(%r14), %eax
    cmpl %eax, %r13d
    ja .Lvpp_jit_invalid
    movl (%r15,%r13), %eax
    jmp .Lvpp_jit_done
.Lvpp_jit_stage_patch:
    JIT_ARG1 %r13d
    call vpp_native_bytes_length
    cmpl $8, %eax
    jne .Lvpp_jit_invalid
    JIT_ARG1 %r13d
    JIT_ARG2 $0
    call vpp_native_bytes_load_i32
    cmpl $4, %r14d
    jb .Lvpp_jit_invalid
    leal -4(%r14), %ecx
    cmpl %ecx, %eax
    ja .Lvpp_jit_invalid
    movl %eax, -80(%rbp)
    JIT_ARG1 %r13d
    JIT_ARG2 $4
    call vpp_native_bytes_load_i32
    movl -80(%rbp), %ecx
    movl %eax, (%r15,%rcx)
.Lvpp_jit_stage_ok:
    xorl %eax, %eax
    jmp .Lvpp_jit_done
.Lvpp_jit_stage_seal:
    testl %r13d, %r13d
    jle .Lvpp_jit_invalid
    cmpl %r14d, %r13d
    ja .Lvpp_jit_invalid
    movl %r14d, %eax
    subl %r13d, %eax
    testl $3, %eax
    jne .Lvpp_jit_invalid
    movl $17, 20(%rbx)
    movl %r13d, %eax
.Lvpp_jit_stage_reloc:
    cmpl %r14d, %eax
    jae .Lvpp_jit_stage_protect
    movl (%r15,%rax), %ecx
    testl $7, %ecx
    jne .Lvpp_jit_invalid
    cmpl $8, %r13d
    jb .Lvpp_jit_invalid
    leal -8(%r13), %edx
    cmpl %edx, %ecx
    ja .Lvpp_jit_invalid
    movq (%r15,%rcx), %rdx
    cmpq %r13, %rdx
    jae .Lvpp_jit_invalid
    addq %r15, %rdx
    movq %rdx, (%r15,%rcx)
    addl $4, %eax
    jmp .Lvpp_jit_stage_reloc
.Lvpp_jit_stage_protect:
#ifdef _WIN32
    movq %r15, %rcx
    movl %r13d, %edx
    movl $0x20, %r8d
    leaq -120(%rbp), %r9
    call VirtualProtect
    testl %eax, %eax
    je .Lvpp_jit_resource
    movq $-1, %rcx
    movq %r15, %rdx
    movl %r13d, %r8d
    call FlushInstructionCache
    testl %eax, %eax
    je .Lvpp_jit_stage_failed_rx
#else
    movq %r15, %rdi
    movl %r13d, %esi
    movl $5, %edx
    movl $10, %eax
    syscall
    testq %rax, %rax
    jne .Lvpp_jit_resource
#endif
    movl %r13d, 8(%rbx)
    movl $5, 20(%rbx)
    movl 16(%rbx), %eax
    jmp .Lvpp_jit_done
#ifdef _WIN32
.Lvpp_jit_stage_failed_rx:
    // Only release is valid after a protection/cache failure.
    movl $17, 20(%rbx)
    jmp .Lvpp_jit_resource
#endif
.Lvpp_jit_release:
#ifdef _WIN32
    movq (%rbx), %rcx
    xorl %edx, %edx
    movl $0x8000, %r8d
    call VirtualFree
    testl %eax, %eax
    je .Lvpp_jit_resource
#else
    movq (%rbx), %rdi
    movl 24(%rbx), %esi
    testl %esi, %esi
    jne 1f
    movl 8(%rbx), %esi
1:  movl $11, %eax
    syscall
    testq %rax, %rax
    jne .Lvpp_jit_resource
#endif
    movq $0, (%rbx)
    movl $0, 16(%rbx)
    movl $0, 20(%rbx)
    movl $0, 24(%rbx)
    xorl %eax, %eax
    jmp .Lvpp_jit_done
.Lvpp_jit_address:
    JIT_ARG1 %r13d
    call vpp_native_bytes_length
    cmpl $8, %eax
    jl .Lvpp_jit_invalid
    cmpl $1, %r12d
    je .Lvpp_jit_abi_1
    cmpl $2, %r12d
    je .Lvpp_jit_abi_2
    cmpl $3, %r12d
    je .Lvpp_jit_abi_3
    cmpl $4, %r12d
    je .Lvpp_jit_abi_4
    cmpl $5, %r12d
    je .Lvpp_jit_abi_5
    cmpl $6, %r12d
    je .Lvpp_jit_abi_6
    cmpl $7, %r12d
    je .Lvpp_jit_abi_7
    cmpl $8, %r12d
    je .Lvpp_jit_abi_8
    cmpl $9, %r12d
    je .Lvpp_jit_abi_9
    cmpl $10, %r12d
    je .Lvpp_jit_abi_10
    cmpl $11, %r12d
    je .Lvpp_jit_abi_11
    cmpl $12, %r12d
    je .Lvpp_jit_abi_12
    cmpl $13, %r12d
    je .Lvpp_jit_abi_13
    cmpl $16, %r12d
    je .Lvpp_jit_abi_16
    cmpl $17, %r12d
    je .Lvpp_jit_abi_17
    cmpl $18, %r12d
    je .Lvpp_jit_abi_18
    cmpl $23, %r12d
    je .Lvpp_jit_abi_23
    cmpl $24, %r12d
    je .Lvpp_jit_abi_24
    cmpl $25, %r12d
    je .Lvpp_jit_abi_25
    cmpl $26, %r12d
    je .Lvpp_jit_abi_26
    cmpl $27, %r12d
    je .Lvpp_jit_abi_27
    cmpl $43, %r12d
    je .Lvpp_jit_abi_43
    cmpl $44, %r12d
    je .Lvpp_jit_abi_44
    cmpl $45, %r12d
    je .Lvpp_jit_abi_45
    cmpl $46, %r12d
    je .Lvpp_jit_abi_46
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
    cmpl $71, %r12d
    je .Lvpp_jit_abi_71
#endif
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
    cmpl $72, %r12d
    je .Lvpp_jit_abi_72
#endif
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
    cmpl $73, %r12d
    je .Lvpp_jit_abi_73
#endif
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
    cmpl $76, %r12d
    je .Lvpp_jit_abi_76
#endif
    cmpl $14, %r12d
    je .Lvpp_jit_abi_14
    cmpl $15, %r12d
    je .Lvpp_jit_abi_15
    cmpl $28, %r12d
    je .Lvpp_jit_abi_28
    cmpl $29, %r12d
    je .Lvpp_jit_abi_29
    cmpl $30, %r12d
    je .Lvpp_jit_abi_30
    cmpl $31, %r12d
    je .Lvpp_jit_abi_31
    cmpl $32, %r12d
    je .Lvpp_jit_abi_32
    cmpl $38, %r12d
    je .Lvpp_jit_abi_38
    cmpl $39, %r12d
    je .Lvpp_jit_abi_39
    cmpl $35, %r12d
    je .Lvpp_jit_abi_35
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
    cmpl $77, %r12d
    je .Lvpp_jit_abi_77
#endif
    cmpl $33, %r12d
    je .Lvpp_jit_abi_33
    cmpl $34, %r12d
    je .Lvpp_jit_abi_34
    cmpl $36, %r12d
    je .Lvpp_jit_abi_36
    cmpl $37, %r12d
    je .Lvpp_jit_abi_37
    cmpl $47, %r12d
    je .Lvpp_jit_abi_47
    cmpl $48, %r12d
    je .Lvpp_jit_abi_48
    cmpl $49, %r12d
    je .Lvpp_jit_abi_49
    cmpl $50, %r12d
    je .Lvpp_jit_abi_50
    cmpl $53, %r12d
    je .Lvpp_jit_abi_53
    cmpl $54, %r12d
    je .Lvpp_jit_abi_54
    cmpl $55, %r12d
    je .Lvpp_jit_abi_55
    cmpl $56, %r12d
    je .Lvpp_jit_abi_56
    cmpl $57, %r12d
    je .Lvpp_jit_abi_57
    cmpl $58, %r12d
    je .Lvpp_jit_abi_58
    cmpl $59, %r12d
    je .Lvpp_jit_abi_59
    cmpl $60, %r12d
    je .Lvpp_jit_abi_60
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
    cmpl $65, %r12d
    je .Lvpp_jit_abi_65
#endif
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
    cmpl $66, %r12d
    je .Lvpp_jit_abi_66
#endif
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
    cmpl $67, %r12d
    je .Lvpp_jit_abi_67
#endif
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
    cmpl $68, %r12d
    je .Lvpp_jit_abi_68
#endif
#if !defined(_WIN32) || (defined(VPP_WINDOWS_BOUNDARY_TEST) && !defined(VPP_WINDOWS_RAW_BOUNDARY))
    cmpl $69, %r12d
    je .Lvpp_jit_abi_69
#endif
#if !defined(_WIN32) || (defined(VPP_WINDOWS_BOUNDARY_TEST) && !defined(VPP_WINDOWS_RAW_BOUNDARY))
    cmpl $70, %r12d
    je .Lvpp_jit_abi_70
#endif
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
    cmpl $74, %r12d
    je .Lvpp_jit_abi_74
#endif
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
    cmpl $75, %r12d
    je .Lvpp_jit_abi_75
#endif
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
    cmpl $78, %r12d
    je .Lvpp_jit_abi_78
#endif
    cmpl $81, %r12d
    je .Lvpp_jit_abi_81
    cmpl $82, %r12d
    je .Lvpp_jit_abi_82
    cmpl $85, %r12d
    je .Lvpp_jit_abi_85
    cmpl $86, %r12d
    je .Lvpp_jit_abi_86
    cmpl $87, %r12d
    je .Lvpp_jit_abi_87
    jmp .Lvpp_jit_invalid
.Lvpp_jit_abi_14:
    leaq vpp_native_argc(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_15:
    leaq vpp_native_argv(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_28:
    leaq vpp_native_memory_read_byte(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_29:
    leaq vpp_native_memory_write_byte(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_30:
    leaq vpp_native_memory_read_i32(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_31:
    leaq vpp_native_memory_write_i32(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_32:
    leaq vpp_native_memory_bind(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_38:
    leaq vpp_native_os_argument_length(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_39:
    leaq vpp_native_os_argument_byte(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_1:
    leaq vpp_native_print_i32(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_2:
    leaq vpp_native_i32_div(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_3:
    leaq vpp_native_bytes_new(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_4:
    leaq vpp_native_bytes_length(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_5:
    leaq vpp_native_bytes_get(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_6:
    leaq vpp_native_bytes_append(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_7:
    leaq vpp_native_bytes_push_i32(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_8:
    leaq vpp_native_bytes_load_i32(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_9:
    leaq vpp_native_bytes_store_i32(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_10:
    leaq vpp_native_bytes_clear(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_11:
    leaq vpp_native_bytes_concat(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_12:
    leaq vpp_native_bytes_equal(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_13:
    leaq vpp_native_bytes_free(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_16:
    leaq vpp_native_file_open_read(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_17:
    leaq vpp_native_file_read_block(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_18:
    leaq vpp_native_file_close(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_23:
    leaq vpp_native_bytes_slice(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_24:
    leaq vpp_native_bytes_find(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_25:
    leaq vpp_native_bytes_print(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_26:
    leaq vpp_native_file_open_write(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_27:
    leaq vpp_native_file_write_block(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_43:
    leaq vpp_native_read_i32(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_44:
    leaq vpp_native_read_status(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_45:
    leaq vpp_native_file_read_byte(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_46:
    leaq vpp_native_file_write_byte(%rip), %r15
    jmp .Lvpp_jit_save_address
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
.Lvpp_jit_abi_71:
    leaq vpp_native_os_clock(%rip), %r15
    jmp .Lvpp_jit_save_address
#endif
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
.Lvpp_jit_abi_72:
    leaq vpp_native_os_sleep(%rip), %r15
    jmp .Lvpp_jit_save_address
#endif
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
.Lvpp_jit_abi_73:
    leaq vpp_native_os_entropy(%rip), %r15
    jmp .Lvpp_jit_save_address
#endif
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
.Lvpp_jit_abi_76:
    leaq vpp_native_os_process(%rip), %r15
    jmp .Lvpp_jit_save_address
#endif
.Lvpp_jit_abi_35:
    leaq vpp_native_os_write(%rip), %r15
    jmp .Lvpp_jit_save_address
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
.Lvpp_jit_abi_77:
    leaq vpp_native_os_socket(%rip), %r15
    jmp .Lvpp_jit_save_address
#endif
.Lvpp_jit_abi_33:
    leaq vpp_native_os_open(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_34:
    leaq vpp_native_os_read(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_36:
    leaq vpp_native_os_close(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_37:
    leaq vpp_native_os_errno(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_47:
    leaq vpp_native_os_home(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_48:
    leaq vpp_native_os_cwd(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_49:
    leaq vpp_native_os_mkstemp(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_50:
    leaq vpp_native_os_rename(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_53:
    leaq vpp_native_os_stat(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_54:
    leaq vpp_native_os_lstat(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_55:
    leaq vpp_native_os_access_exec(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_56:
    leaq vpp_native_os_unlink(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_57:
    leaq vpp_native_os_env_copy(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_58:
    leaq vpp_native_os_executable_path(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_59:
    leaq vpp_native_os_mkdir(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_60:
    leaq vpp_native_os_fstat(%rip), %r15
    jmp .Lvpp_jit_save_address
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
.Lvpp_jit_abi_65:
    leaq vpp_native_f64_from_i32(%rip), %r15
    jmp .Lvpp_jit_save_address
#endif
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
.Lvpp_jit_abi_66:
    leaq vpp_native_f64_binary(%rip), %r15
    jmp .Lvpp_jit_save_address
#endif
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
.Lvpp_jit_abi_67:
    leaq vpp_native_f64_compare(%rip), %r15
    jmp .Lvpp_jit_save_address
#endif
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
.Lvpp_jit_abi_68:
    leaq vpp_native_f64_to_i32(%rip), %r15
    jmp .Lvpp_jit_save_address
#endif
#if !defined(_WIN32) || (defined(VPP_WINDOWS_BOUNDARY_TEST) && !defined(VPP_WINDOWS_RAW_BOUNDARY))
.Lvpp_jit_abi_69:
    leaq vpp_native_f64_parse(%rip), %r15
    jmp .Lvpp_jit_save_address
#endif
#if !defined(_WIN32) || (defined(VPP_WINDOWS_BOUNDARY_TEST) && !defined(VPP_WINDOWS_RAW_BOUNDARY))
.Lvpp_jit_abi_70:
    leaq vpp_native_f64_text(%rip), %r15
    jmp .Lvpp_jit_save_address
#endif
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
.Lvpp_jit_abi_74:
    leaq vpp_native_os_identity(%rip), %r15
    jmp .Lvpp_jit_save_address
#endif
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
.Lvpp_jit_abi_75:
    leaq vpp_native_os_directory(%rip), %r15
    jmp .Lvpp_jit_save_address
#endif
#if !defined(_WIN32) || defined(VPP_WINDOWS_BOUNDARY_TEST)
.Lvpp_jit_abi_78:
    leaq vpp_native_os_thread_cpu_clock(%rip), %r15
    jmp .Lvpp_jit_save_address
#endif
.Lvpp_jit_abi_81:
    leaq vpp_native_os_fchmod(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_82:
    leaq vpp_native_memory_zero(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_85:
    leaq vpp_native_memory_copy(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_86:
    leaq vpp_native_memory_ascii_prefix(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_abi_87:
    leaq vpp_native_os_seek(%rip), %r15
    jmp .Lvpp_jit_save_address
.Lvpp_jit_save_address:
    JIT_ARG1 %r13d
    JIT_ARG2 $0
    JIT_ARG3 %r15d
    call vpp_native_bytes_store_i32
    testl %eax, %eax
    jne .Lvpp_jit_invalid
    shrq $32, %r15
    JIT_ARG1 %r13d
    JIT_ARG2 $4
    JIT_ARG3 %r15d
    call vpp_native_bytes_store_i32
    jmp .Lvpp_jit_done
.Lvpp_jit_invalid:
    movl $-1, %eax
    jmp .Lvpp_jit_done
.Lvpp_jit_resource:
    movl $-2, %eax
.Lvpp_jit_done:
    addq $216, %rsp
    popq %rsi
    popq %rdi
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
// Internal lookup; no user-visible raw pointer return.
.globl vpp_native_jit_image
vpp_native_jit_image:
#ifdef _WIN32
    movl %ecx, %eax
#else
    movl %edi, %eax
#endif
    testl %eax, %eax
    jle 3f
    leaq vpp_jit_slots(%rip), %r10
    movl $64, %r11d
1:  cmpl 16(%r10), %eax
    je 2f
    addq $32, %r10
    decl %r11d
    jne 1b
    jmp 3f
2:  cmpl $5, 20(%r10)
    jne 3f
    movl 8(%r10), %edx
    movq (%r10), %rax
    ret
3:  xorl %eax, %eax
    xorl %edx, %edx
    ret
.bss
.p2align 3
vpp_jit_slots: .zero 2048
vpp_jit_next: .zero 4
.text
.purgem JIT_ARG1
.purgem JIT_ARG2
.purgem JIT_ARG3
