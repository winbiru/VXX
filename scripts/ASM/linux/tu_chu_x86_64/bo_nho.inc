// Cấp vùng nhớ, kiểm tra giới hạn và thao tác byte/i32/con trỏ.

.text
.p2align 4
.globl vpp_native_memory_base
vpp_native_memory_base:
    movq vpp_native_arena_base(%rip), %rax
    testq %rax, %rax
    jne 1f
    subq $8, %rsp
    xorl %edi, %edi
    movl $135271424, %esi
    movl $3, %edx
    movl $0x22, %ecx
    movq $-1, %r8
    xorl %r9d, %r9d
    movl %ecx, %r10d
    movl $9, %eax
    syscall
    addq $8, %rsp
    cmpq $-4095, %rax
    jae 2f
    movq %rax, vpp_native_arena_base(%rip)
1:
    ret
2:
    xorl %eax, %eax
    ret
.globl vpp_native_memory_read_byte
vpp_native_memory_read_byte:
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    cmpl $(135271424 - 1), %edi
    ja Lbo_nho_loi
    movzbl (%r11,%rdi), %eax
    leave
    ret
.globl vpp_native_memory_write_byte
vpp_native_memory_write_byte:
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    cmpl $(135271424 - 1), %edi
    ja Lbo_nho_loi
    cmpl $255, %esi
    ja Lbo_nho_loi
    movb %sil, (%r11,%rdi)
    xorl %eax, %eax
    leave
    ret
.globl vpp_native_memory_read_i32
vpp_native_memory_read_i32:
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    cmpl $(135271424 - 4), %edi
    ja Lbo_nho_loi
    movl (%r11,%rdi), %eax
    leave
    ret
.globl vpp_native_memory_write_i32
vpp_native_memory_write_i32:
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    cmpl $(135271424 - 4), %edi
    ja Lbo_nho_loi
    movl %esi, (%r11,%rdi)
    xorl %eax, %eax
    leave
    ret
// Xóa vùng nhớ; ABI SysV (edi=lệch, esi=số byte).
.globl vpp_native_memory_zero
vpp_native_memory_zero:
    pushq %rbp
    movq %rsp, %rbp
    subq $16, %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    call vpp_native_memory_base
    testq %rax, %rax
    je Lbo_nho_loi
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    testl %esi, %esi
    js Lbo_nho_loi
    movl $135271424, %eax
    subl %esi, %eax
    jb Lbo_nho_loi
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
    leave
    ret
.globl vpp_native_memory_bind
vpp_native_memory_bind:
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    cmpl $(135271424 - 8), %edi
    ja Lbo_nho_loi
    testl $7, %edi
    jnz Lbo_nho_loi
    cmpl $(135271424 - 1), %esi
    ja Lbo_nho_loi
    leaq (%r11,%rsi), %rax
    movq %rax, (%r11,%rdi)
    xorl %eax, %eax
    leave
    ret
Lbo_nho_loi:
    movl $-1, %eax
    leave
    ret


// Checked memmove semantics, with SSE2 vector blocks.
.globl vpp_native_memory_copy
vpp_native_memory_copy:
    pushq %rbp
    movq %rsp, %rbp
    subq $80, %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    movl %edi, %r9d
    movl %esi, %r10d
    movl %edx, %r8d
    testl %r8d, %r8d
    js Lbo_nho_loi
    movl $135271424, %eax
    subl %r8d, %eax
    jb Lbo_nho_loi
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
    // movdqu (%r10), %xmm0; NOP padding for older seed
    .long 0x6f0f41f3
    .long 0x90909002
    // movdqu %xmm0, (%r9); NOP padding for older seed
    .long 0x7f0f41f3
    .long 0x90909001
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
    // movdqu (%r10), %xmm0; NOP padding for older seed
    .long 0x6f0f41f3
    .long 0x90909002
    // movdqu %xmm0, (%r9); NOP padding for older seed
    .long 0x7f0f41f3
    .long 0x90909001
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
    leave
    ret

.globl vpp_native_memory_ascii_prefix
vpp_native_memory_ascii_prefix:
    pushq %rbp
    movq %rsp, %rbp
    subq $80, %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    movl %edi, %r9d
    movl %esi, %r8d
    testl %r8d, %r8d
    js Lbo_nho_loi
    movl $135271424, %eax
    subl %r8d, %eax
    jb Lbo_nho_loi
    cmpl %eax, %r9d
    ja Lbo_nho_loi
    leaq (%r11,%r9), %r9
    xorl %eax, %eax
Lbulk_ascii_vector:
    cmpl $16, %r8d
    jb Lbulk_ascii_tail
    // movdqu (%r9), %xmm0; NOP padding for older seed
    .long 0x6f0f41f3
    .long 0x90909001
    // pmovmskb %xmm0, %ecx; NOP padding for older seed
    .long 0xc8d70f66
    testl %ecx, %ecx
    jnz Lbulk_ascii_hit
    addq $16, %r9
    addl $16, %eax
    subl $16, %r8d
    jmp Lbulk_ascii_vector
Lbulk_ascii_hit:
    // bsfl %ecx, %ecx; NOP padding for older seed
    .long 0x90c9bc0f
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
    leave
    ret
