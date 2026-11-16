// Arena fast path is shared only before OS workers are enabled.
.text
.p2align 4
.globl PUBLIC(vpp_native_memory_base)
PUBLIC(vpp_native_memory_base):
    movq PUBLIC(vpp_native_arena_base)(%rip), %rax
    testq %rax, %rax
    jnz Lmemory_base_return
    pushq %rbx
    cmpl $0, PUBLIC(vpp_native_arena_tls_enabled)(%rip)
    je Lmemory_base_allocate
    call Lworker_os_arena
    testq %rax, %rax
    jnz Lmemory_base_pop
Lmemory_base_allocate:
    movl $VPP_ARENA_SIZE, %edi
    call Lworker_os_map
    testq %rax, %rax
    jz Lmemory_base_pop
    cmpl $0, PUBLIC(vpp_native_arena_tls_enabled)(%rip)
    jne Lmemory_base_tls
    movq %rax, PUBLIC(vpp_native_arena_base)(%rip)
    jmp Lmemory_base_pop
Lmemory_base_tls:
    movq %rax, %rbx
    movq %rax, %rdi
    call Lworker_os_set_arena
    testl %eax, %eax
    jnz Lmemory_base_release
    movq %rbx, %rax
    jmp Lmemory_base_pop
Lmemory_base_release:
    movq %rbx, %rdi
    movl $VPP_ARENA_SIZE, %esi
    call Lworker_os_unmap
    xorl %eax, %eax
Lmemory_base_pop:
    popq %rbx
Lmemory_base_return:
    ret
.globl PUBLIC(vpp_native_memory_current)
PUBLIC(vpp_native_memory_current):
    cmpl $0, PUBLIC(vpp_native_arena_tls_enabled)(%rip)
    jne Lworker_os_arena
    movq PUBLIC(vpp_native_arena_base)(%rip), %rax
    ret
.globl PUBLIC(vpp_native_arena_enable_threads)
PUBLIC(vpp_native_arena_enable_threads):
    cmpl $0, PUBLIC(vpp_native_arena_tls_enabled)(%rip)
    jne 1f
    subq $8, %rsp
    call PUBLIC(vpp_native_memory_base)
    testq %rax, %rax
    jz 2f
    movq %rax, %rdi
    call Lworker_os_enable
    testl %eax, %eax
    jnz 2f
    movl $1, PUBLIC(vpp_native_arena_tls_enabled)(%rip)
    movq $0, PUBLIC(vpp_native_arena_base)(%rip)
    addq $8, %rsp
1:  xorl %eax, %eax
    ret
2:  movl $-1, %eax
    addq $8, %rsp
    ret

// Giữ ba đối số 32 bit trong lúc mmap khởi tạo vùng nhớ.
.macro VAO_BO_NHO
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    // Chỉ khởi tạo arena ở lần truy cập đầu tiên.
    movq PUBLIC(vpp_native_arena_base)(%rip), %r11
    testq %r11, %r11
    jne 1f
    call PUBLIC(vpp_native_memory_base)
    movq %rax, %r11
1:
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
.endm
.macro TRA_BO_NHO
    leave
    ret
.endm
.macro KIEM_TRA_LECH thanh_ghi, do_rong
    cmpl $(VPP_ARENA_SIZE - \do_rong), \thanh_ghi
    ja Lbo_nho_loi                 // So sánh không dấu cũng từ chối số âm
.endm
.macro KIEM_TRA_DUONG thanh_ghi
    KIEM_TRA_LECH \thanh_ghi, VPP_PATH_MAX
.endm

.globl PUBLIC(vpp_native_memory_read_byte)
PUBLIC(vpp_native_memory_read_byte):
    VAO_BO_NHO
    KIEM_TRA_LECH %edi, 1
    movzbl (%r11,%rdi), %eax
    TRA_BO_NHO
.globl PUBLIC(vpp_native_memory_write_byte)
PUBLIC(vpp_native_memory_write_byte):
    VAO_BO_NHO
    KIEM_TRA_LECH %edi, 1
    cmpl $255, %esi
    ja Lbo_nho_loi
    movb %sil, (%r11,%rdi)
    xorl %eax, %eax
    TRA_BO_NHO
.globl PUBLIC(vpp_native_memory_read_i32)
PUBLIC(vpp_native_memory_read_i32):
    VAO_BO_NHO
    KIEM_TRA_LECH %edi, 4
    movl (%r11,%rdi), %eax
    TRA_BO_NHO
.globl PUBLIC(vpp_native_memory_write_i32)
PUBLIC(vpp_native_memory_write_i32):
    VAO_BO_NHO
    KIEM_TRA_LECH %edi, 4
    movl %esi, (%r11,%rdi)
    xorl %eax, %eax
    TRA_BO_NHO
// Xóa vùng nhớ một lần, độ dài tính bằng byte.
.globl PUBLIC(vpp_native_memory_zero)
PUBLIC(vpp_native_memory_zero):
    VAO_BO_NHO
    testl %esi, %esi
    js Lbo_nho_loi
    movl $VPP_ARENA_SIZE, %eax
    subl %esi, %eax
    jc Lbo_nho_loi
    cmpl %eax, %edi
    ja Lbo_nho_loi
    addq %r11, %rdi
    movl %esi, %ecx
    shrl $3, %ecx
    xorl %eax, %eax
    rep stosq
    movl %esi, %ecx
    andl $7, %ecx
    rep stosb
    xorl %eax, %eax
    TRA_BO_NHO
.globl PUBLIC(vpp_native_memory_bind)
PUBLIC(vpp_native_memory_bind):
    VAO_BO_NHO
    KIEM_TRA_LECH %edi, 8
    testl $7, %edi
    jnz Lbo_nho_loi
    KIEM_TRA_LECH %esi, 1
    leaq (%r11,%rsi), %rax
    movq %rax, (%r11,%rdi)      // Giữ đầy đủ địa chỉ 64 bit
    xorl %eax, %eax
    TRA_BO_NHO
Lbo_nho_loi:
#ifdef VPP_MEMORY_SET_ERRNO
    movl $22, %edi
    call Lworker_set_errno
#endif
    movl $-1, %eax
    TRA_BO_NHO


// Checked memmove semantics, with SSE2 vector blocks.
.globl PUBLIC(vpp_native_memory_copy)
PUBLIC(vpp_native_memory_copy):
    VAO_BO_NHO
    movl %edi, %r9d
    movl %esi, %r10d
    movl %edx, %r8d
    testl %r8d, %r8d
    js Lbo_nho_loi
    movl $VPP_ARENA_SIZE, %eax
    subl %r8d, %eax
    jc Lbo_nho_loi
    cmpl %eax, %r9d
    ja Lbo_nho_loi
    cmpl %eax, %r10d
    ja Lbo_nho_loi
    testl %r8d, %r8d
    jz Lbulk_copy_done
    cmpl %r10d, %r9d
    je Lbulk_copy_done
    jb Lbulk_copy_forward_prepare
    movl %r9d, %eax
    subl %r10d, %eax
    cmpl %r8d, %eax
    jae Lbulk_copy_forward_prepare
    leaq (%r11,%r9), %r9
    leaq (%r11,%r10), %r10
    addq %r8, %r9
    addq %r8, %r10
Lbulk_copy_backward:
    cmpl $16, %r8d
    jb Lbulk_copy_backward_tail
    subq $16, %r10
    subq $16, %r9
    movdqu (%r10), %xmm0
    movdqu %xmm0, (%r9)
    subl $16, %r8d
    jmp Lbulk_copy_backward
Lbulk_copy_backward_tail:
    testl %r8d, %r8d
    jz Lbulk_copy_done
    decq %r10
    decq %r9
    movb (%r10), %al
    movb %al, (%r9)
    decl %r8d
    jmp Lbulk_copy_backward_tail
Lbulk_copy_forward_prepare:
    leaq (%r11,%r9), %r9
    leaq (%r11,%r10), %r10
Lbulk_copy_forward:
    cmpl $16, %r8d
    jb Lbulk_copy_forward_tail
    movdqu (%r10), %xmm0
    movdqu %xmm0, (%r9)
    addq $16, %r10
    addq $16, %r9
    subl $16, %r8d
    jmp Lbulk_copy_forward
Lbulk_copy_forward_tail:
    testl %r8d, %r8d
    jz Lbulk_copy_done
    movb (%r10), %al
    movb %al, (%r9)
    incq %r10
    incq %r9
    decl %r8d
    jmp Lbulk_copy_forward_tail
Lbulk_copy_done:
    xorl %eax, %eax
    TRA_BO_NHO

.globl PUBLIC(vpp_native_memory_ascii_prefix)
PUBLIC(vpp_native_memory_ascii_prefix):
    VAO_BO_NHO
    movl %edi, %r9d
    movl %esi, %r8d
    testl %r8d, %r8d
    js Lbo_nho_loi
    movl $VPP_ARENA_SIZE, %eax
    subl %r8d, %eax
    jc Lbo_nho_loi
    cmpl %eax, %r9d
    ja Lbo_nho_loi
    leaq (%r11,%r9), %r9
    xorl %eax, %eax
Lbulk_ascii_vector:
    cmpl $16, %r8d
    jb Lbulk_ascii_tail
    movdqu (%r9), %xmm0
    pmovmskb %xmm0, %ecx
    testl %ecx, %ecx
    jnz Lbulk_ascii_hit
    addq $16, %r9
    addl $16, %eax
    subl $16, %r8d
    jmp Lbulk_ascii_vector
Lbulk_ascii_hit:
    bsfl %ecx, %ecx
    addl %ecx, %eax
    jmp Lbulk_ascii_done
Lbulk_ascii_tail:
    testl %r8d, %r8d
    jz Lbulk_ascii_done
    cmpb $0, (%r9)
    js Lbulk_ascii_done
    incq %r9
    incl %eax
    decl %r8d
    jmp Lbulk_ascii_tail
Lbulk_ascii_done:
    TRA_BO_NHO

// JIT swaps only the calling thread's arena.
.globl PUBLIC(vpp_native_jit_swap_arena)
PUBLIC(vpp_native_jit_swap_arena):
    cmpl $0, PUBLIC(vpp_native_arena_tls_enabled)(%rip)
    jne Larena_swap_tls
    movq PUBLIC(vpp_native_arena_base)(%rip), %rax
    movq %rdi, PUBLIC(vpp_native_arena_base)(%rip)
    ret
Larena_swap_tls:
    pushq %rbx
    pushq %r12
    subq $8, %rsp
    movq %rdi, %rbx
    call Lworker_os_arena
    movq %rax, %r12
    movq %rbx, %rdi
    call Lworker_os_set_arena
    testl %eax, %eax
    jnz 1f
    movq %r12, %rax
    addq $8, %rsp
    popq %r12
    popq %rbx
    ret
1:  ud2
#ifdef __APPLE__
.section __DATA,__bss
#else
.bss
#endif
.p2align 2
PUBLIC(vpp_native_arena_tls_enabled): .zero 4
.text
