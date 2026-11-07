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
