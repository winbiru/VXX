// Hàm bọc nhập xuất và chuyển kết quả lời gọi hệ thống.

.macro NHAP_XUAT_CRT ten, dich
    .globl \ten
\ten:
    VAO_BO_NHO
    KIEM_TRA_LECH %edx, 0
    cmpl $(0), %r8d
    jl Lbo_nho_loi
    movl %edx, %eax
    addq %r8, %rax
    cmpq $(VPP_ARENA_SIZE), %rax
    ja Lbo_nho_loi
    leaq (%r11,%rdx), %rdx
    call \dich
    TRA_BO_NHO
.endm
NHAP_XUAT_CRT vpp_native_os_read, _read
NHAP_XUAT_CRT vpp_native_os_write, _write
