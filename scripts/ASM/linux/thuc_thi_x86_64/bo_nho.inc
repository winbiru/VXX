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

