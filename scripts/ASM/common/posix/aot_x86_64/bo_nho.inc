// Cấp vùng nhớ, kiểm tra giới hạn và thao tác byte/i32/con trỏ.

.text
.p2align 4
.globl PUBLIC(vpp_native_memory_base)
PUBLIC(vpp_native_memory_base):
    movq PUBLIC(vpp_native_arena_base)(%rip), %rax
    testq %rax, %rax
    jne 1f
    subq $8, %rsp                 // Căn ngăn xếp cho lời gọi SysV
    xorl %edi, %edi
    movl $VPP_ARENA_SIZE, %esi
    movl $3, %edx                // PROT_READ | PROT_WRITE
#ifdef __APPLE__
    movl $0x1002, %ecx          // MAP_PRIVATE | MAP_ANON (Darwin)
#else
    movl $0x22, %ecx            // MAP_PRIVATE | MAP_ANONYMOUS (Linux)
#endif
    movq $-1, %r8
    xorl %r9d, %r9d
#ifdef __APPLE__
    call LIB(mmap)
#else
    movl %ecx, %r10d             // Đối số syscall Linux thứ tư: cờ
    movl $9, %eax                // SYS_mmap
    syscall
#endif
    addq $8, %rsp
#ifdef __APPLE__
    cmpq $-1, %rax
    je 2f
#else
    cmpq $-4095, %rax           // Linux trả mã lỗi âm -errno
    jae 2f
#endif
    movq %rax, PUBLIC(vpp_native_arena_base)(%rip)
1:  ret
2:  xorl %eax, %eax
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
    movl $-1, %eax
    TRA_BO_NHO

