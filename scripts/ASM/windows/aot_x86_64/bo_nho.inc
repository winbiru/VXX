// Cấp vùng nhớ, kiểm tra giới hạn và thao tác byte/i32/con trỏ.

.text
.p2align 4
.globl vpp_native_memory_base
vpp_native_memory_base:
    movq vpp_native_arena_base(%rip), %rax
    testq %rax, %rax
    jne 1f
    subq $40, %rsp          // 32 byte dự phòng đối số và phần căn lời gọi
    xorl %ecx, %ecx
    movl $VPP_ARENA_SIZE, %edx
    movl $0x3000, %r8d      // MEM_RESERVE | MEM_COMMIT
    movl $4, %r9d           // PAGE_READWRITE
    call VirtualAlloc
    addq $40, %rsp
    testq %rax, %rax
    je 1f
    movq %rax, vpp_native_arena_base(%rip)
1:  ret

.macro VAO_BO_NHO
    pushq %rbp
    movq %rsp, %rbp
    subq $(96), %rsp       // Gồm vùng đối số dự phòng
    movl %ecx, -4(%rbp)
    movl %edx, -8(%rbp)
    movl %r8d, -12(%rbp)
    // Tránh gọi lại hàm lấy arena khi VirtualAlloc đã thành công.
    movq vpp_native_arena_base(%rip), %r11
    testq %r11, %r11
    jne 1f
    call vpp_native_memory_base
    movq %rax, %r11
1:
    movl -4(%rbp), %ecx
    movl -8(%rbp), %edx
    movl -12(%rbp), %r8d
    testq %r11, %r11
    je Lbo_nho_loi
.endm
.macro TRA_BO_NHO
    leave
    ret
.endm
.macro KIEM_TRA_LECH thanh_ghi, do_rong
    cmpl $(VPP_ARENA_SIZE - \do_rong), \thanh_ghi
    ja Lbo_nho_loi
.endm
.macro KIEM_TRA_DUONG thanh_ghi
    KIEM_TRA_LECH \thanh_ghi, 4096
.endm
.globl vpp_native_memory_read_byte
vpp_native_memory_read_byte:
    VAO_BO_NHO
    KIEM_TRA_LECH %ecx, 1
    movzbl (%r11,%rcx), %eax
    TRA_BO_NHO
.globl vpp_native_memory_write_byte
vpp_native_memory_write_byte:
    VAO_BO_NHO
    KIEM_TRA_LECH %ecx, 1
    cmpl $255, %edx
    ja Lbo_nho_loi
    movb %dl, (%r11,%rcx)
    xorl %eax, %eax
    TRA_BO_NHO
.globl vpp_native_memory_read_i32
vpp_native_memory_read_i32:
    VAO_BO_NHO
    KIEM_TRA_LECH %ecx, 4
    movl (%r11,%rcx), %eax
    TRA_BO_NHO
.globl vpp_native_memory_write_i32
vpp_native_memory_write_i32:
    VAO_BO_NHO
    KIEM_TRA_LECH %ecx, 4
    movl %edx, (%r11,%rcx)
    xorl %eax, %eax
    TRA_BO_NHO
// Xóa vùng nhớ bằng khối 8 byte; chỉ dùng thanh ghi tạm Win64.
.globl vpp_native_memory_zero
vpp_native_memory_zero:
    VAO_BO_NHO
    testl %edx, %edx
    js Lbo_nho_loi
    movl $VPP_ARENA_SIZE, %eax
    subl %edx, %eax
    jc Lbo_nho_loi
    cmpl %eax, %ecx
    ja Lbo_nho_loi
    leaq (%r11,%rcx), %r9
    movl %edx, %r10d
1:  cmpl $8, %r10d
    jb 2f
    movq $0, (%r9)
    addq $8, %r9
    subl $8, %r10d
    jmp 1b
2:  testl %r10d, %r10d
    jz 3f
    movb $0, (%r9)
    incq %r9
    decl %r10d
    jmp 2b
3:  xorl %eax, %eax
    TRA_BO_NHO
.globl vpp_native_memory_bind
vpp_native_memory_bind:
    VAO_BO_NHO
    KIEM_TRA_LECH %ecx, 8
    testl $7, %ecx
    jnz Lbo_nho_loi
    KIEM_TRA_LECH %edx, 1
    leaq (%r11,%rdx), %rax
    movq %rax, (%r11,%rcx)   // Con trỏ đầy đủ 64 bit
    xorl %eax, %eax
    TRA_BO_NHO
Lbo_nho_loi:
    movl $-1, %eax
    TRA_BO_NHO

