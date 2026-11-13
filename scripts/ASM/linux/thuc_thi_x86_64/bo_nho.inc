// Cấp vùng nhớ, kiểm tra giới hạn và thao tác byte/i32/con trỏ.

.text
.p2align 4
.globl vpp_native_memory_base
.type vpp_native_memory_base, @function
vpp_native_memory_base:
    movq vpp_native_arena_base(%rip), %rax
    testq %rax, %rax
    jne .Lco_so_xong
    pushq %rbp
    movq %rsp, %rbp
    subq $16, %rsp
    xorq %rdi, %rdi
    movl $VPP_LINUX_ARENA_SIZE, %esi
    movl $3, %edx                 // PROT_READ | PROT_WRITE
    movl $0x22, %ecx              // MAP_PRIVATE | MAP_ANONYMOUS
    movq $-1, %r8
    movq $0, (%rsp)               // Đối số mmap thứ sáu: độ lệch = 0
    call vpp_sys_mmap
    cmpq $-4095, %rax
    jae .Lco_so_da_loi
    movq %rax, vpp_native_arena_base(%rip)
    leave
.Lco_so_xong:
    ret
.Lco_so_da_loi:
    negl %eax
    movl %eax, vpp_linux_errno(%rip)  // Giữ nguyên errno thực tế của mmap
    xorl %eax, %eax
    leave
    ret
.size vpp_native_memory_base, .-vpp_native_memory_base

.macro VPP_VAO_BO_NHO
    pushq %rbp
    movq %rsp, %rbp
    subq $48, %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    testq %rax, %rax
    je .Lbo_nho_chua_co
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
.endm
.macro VPP_KIEM_TRA_LECH thanh_ghi, do_rong
    cmpl $(VPP_LINUX_ARENA_SIZE - \do_rong), \thanh_ghi
    ja .Lbo_nho_khong_hop_le
.endm
.macro VPP_KIEM_TRA_DUONG thanh_ghi
    VPP_KIEM_TRA_LECH \thanh_ghi, VPP_LINUX_PATH_MAX
    movl \thanh_ghi, %eax
    leaq (%r11,%rax), %rax
    xorl %ecx, %ecx
1:  cmpb $0, (%rax,%rcx)
    je 2f
    incl %ecx
    cmpl $VPP_LINUX_PATH_MAX, %ecx
    jb 1b
    jmp .Lbo_nho_khong_hop_le
2:
.endm
.macro VPP_TRA_BO_NHO
    leave
    ret
.endm
.macro VPP_TRA_SYSCALL
    call .Lchuan_hoa_goi_he_thong
    VPP_TRA_BO_NHO
.endm

.globl vpp_native_memory_read_byte
vpp_native_memory_read_byte:
    VPP_VAO_BO_NHO
    VPP_KIEM_TRA_LECH %edi, 1
    movzbl (%r11,%rdi), %eax
    VPP_TRA_BO_NHO
.globl vpp_native_memory_write_byte
vpp_native_memory_write_byte:
    VPP_VAO_BO_NHO
    VPP_KIEM_TRA_LECH %edi, 1
    cmpl $255, %esi
    ja .Lbo_nho_khong_hop_le
    movb %sil, (%r11,%rdi)
    xorl %eax, %eax
    VPP_TRA_BO_NHO
.globl vpp_native_memory_read_i32
vpp_native_memory_read_i32:
    VPP_VAO_BO_NHO
    VPP_KIEM_TRA_LECH %edi, 4
    movl (%r11,%rdi), %eax
    VPP_TRA_BO_NHO
.globl vpp_native_memory_write_i32
vpp_native_memory_write_i32:
    VPP_VAO_BO_NHO
    VPP_KIEM_TRA_LECH %edi, 4
    movl %esi, (%r11,%rdi)
    xorl %eax, %eax
    VPP_TRA_BO_NHO
// Xóa vùng nhớ theo số byte, lỗi khi vượt giới hạn arena.
.globl vpp_native_memory_zero
vpp_native_memory_zero:
    VPP_VAO_BO_NHO
    testl %esi, %esi
    js .Lbo_nho_khong_hop_le
    movl $VPP_LINUX_ARENA_SIZE, %eax
    subl %esi, %eax
    jc .Lbo_nho_khong_hop_le
    cmpl %eax, %edi
    ja .Lbo_nho_khong_hop_le
    addq %r11, %rdi
    movl %esi, %ecx
    shrl $3, %ecx
    xorl %eax, %eax
    rep stosq
    movl %esi, %ecx
    andl $7, %ecx
    rep stosb
    xorl %eax, %eax
    VPP_TRA_BO_NHO
.globl vpp_native_memory_bind
vpp_native_memory_bind:
    VPP_VAO_BO_NHO
    VPP_KIEM_TRA_LECH %edi, 8
    testl $7, %edi
    jne .Lbo_nho_khong_hop_le
    VPP_KIEM_TRA_LECH %esi, 1
    leaq (%r11,%rsi), %rax
    movq %rax, (%r11,%rdi)
    xorl %eax, %eax
    VPP_TRA_BO_NHO
.Lbo_nho_khong_hop_le:
    movl $22, vpp_linux_errno(%rip)  // EINVAL khi đối số vùng nhớ không hợp lệ
.Lbo_nho_chua_co:
    movl $-1, %eax
    VPP_TRA_BO_NHO


// Checked memmove semantics, with SSE2 vector blocks.
.globl vpp_native_memory_copy
vpp_native_memory_copy:
    VPP_VAO_BO_NHO
    movl %edi, %r9d
    movl %esi, %r10d
    movl %edx, %r8d
    testl %r8d, %r8d
    js .Lbo_nho_khong_hop_le
    movl $VPP_LINUX_ARENA_SIZE, %eax
    subl %r8d, %eax
    jc .Lbo_nho_khong_hop_le
    cmpl %eax, %r9d
    ja .Lbo_nho_khong_hop_le
    cmpl %eax, %r10d
    ja .Lbo_nho_khong_hop_le
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
    VPP_TRA_BO_NHO

.globl vpp_native_memory_ascii_prefix
vpp_native_memory_ascii_prefix:
    VPP_VAO_BO_NHO
    movl %edi, %r9d
    movl %esi, %r8d
    testl %r8d, %r8d
    js .Lbo_nho_khong_hop_le
    movl $VPP_LINUX_ARENA_SIZE, %eax
    subl %r8d, %eax
    jc .Lbo_nho_khong_hop_le
    cmpl %eax, %r9d
    ja .Lbo_nho_khong_hop_le
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
    VPP_TRA_BO_NHO
