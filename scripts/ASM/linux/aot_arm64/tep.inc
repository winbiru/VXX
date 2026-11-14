// Đọc/ghi, mở/đóng và thao tác tệp.

.macro HAM_NHAP_XUAT ten, dich
    .globl \ten
\ten:
    VAO_BO_NHO
    cmp w1, #0
    b.lt Lbo_nho_loi
    cmp w2, #0
    b.lt Lbo_nho_loi
    uxtw x1, w1
    uxtw x2, w2
    add x10, x1, x2
    mov x11, #0x1400
    movk x11, #0x410, lsl #16
    cmp x10, x11
    b.hi Lbo_nho_loi
    add x1, x9, x1
    bl \dich
    TRA_BO_NHO
.endm
HAM_NHAP_XUAT vpp_native_os_read, read
HAM_NHAP_XUAT vpp_native_os_write, write

.globl vpp_native_os_open
vpp_native_os_open:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    add x0, x9, w0, uxtw
// Đổi cờ mở tệp O_WRONLY|O_CREAT|O_TRUNC từ Darwin (1537) sang Linux (577).
    mov w10, #1537
    cmp w1, w10
    b.ne 1f
    mov w1, #577
    b 2f
1:
    cmp w1, #521               // Darwin O_WRONLY|O_CREAT|O_APPEND
    b.ne 2f
    mov w1, #1089              // Linux O_WRONLY|O_CREAT|O_APPEND
2:
    mov w2, #438               // Quyền open(2) Linux: 0666
    bl open
    TRA_BO_NHO
.globl vpp_native_os_close
vpp_native_os_close:
    b close
// Signed i32 offset; do not truncate a successful 64-bit OS position.
// On EOVERFLOW the underlying file cursor may already have moved.
.globl vpp_native_os_seek
vpp_native_os_seek:
    str x30, [sp, #-16]!
    sxtw x1, w1
    bl lseek
    cmp x0, #0
    b.lt 1f
    lsr x9, x0, #31
    cbz x9, 1f
    bl __errno_location
    mov w9, #75
    str w9, [x0]
    mov w0, #-1
1:
    ldr x30, [sp], #16
    ret
.globl vpp_native_os_errno
vpp_native_os_errno:
    str x30, [sp, #-16]!
    bl __errno_location
    ldr w0, [x0]
    ldr x30, [sp], #16
    ret
.globl vpp_native_os_mkstemp
vpp_native_os_mkstemp:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    add x0, x9, w0, uxtw
    bl mkstemp
    TRA_BO_NHO
.globl vpp_native_os_rename
vpp_native_os_rename:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    mov w12, w0
    mov w0, w1
    KIEM_TRA_BIEN 4096
    add x1, x9, w1, uxtw
    add x0, x9, w12, uxtw
    bl rename
    TRA_BO_NHO

