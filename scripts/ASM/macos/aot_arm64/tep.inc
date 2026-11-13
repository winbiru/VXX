// Đọc/ghi, mở/đóng và thao tác tệp.

.macro HAM_NHAP_XUAT ten, dich
    .globl _\ten
_\ten:
    stp x0, x1, [sp, #-32]!
    stp x30, x2, [sp, #16]
    bl _vpp_native_memory_base
    mov x9, x0
    ldp x0, x1, [sp]
    ldr x2, [sp, #24]
    cbz x9, Lnhap_xuat_loi
    cmp w1, #0
    b.lt Lnhap_xuat_loi
    cmp w2, #0
    b.lt Lnhap_xuat_loi
    uxtw x1, w1
    uxtw x2, w2
    add x10, x1, x2
    mov x11, #0x1400
    movk x11, #0x810, lsl #16
    cmp x10, x11
    b.hi Lnhap_xuat_loi
    add x1, x9, x1
    bl _\dich
    TRA_BO_NHO
.endm
HAM_NHAP_XUAT vpp_native_os_read, read
HAM_NHAP_XUAT vpp_native_os_write, write
Lnhap_xuat_loi:
    mov w0, #-1
    TRA_BO_NHO

.globl _vpp_native_os_open
_vpp_native_os_open:
    VAO_BO_NHO
    KIEM_TRA_BIEN 1
    add x0, x9, w0, uxtw
// Darwin truyền đối số biến thiên trên ngăn xếp, kể cả quyền mở tệp.
    mov x10, #438            // 0666, chịu ảnh hưởng umask của tiến trình
    str x10, [sp]
    bl _open
    TRA_BO_NHO
.globl _vpp_native_os_close
_vpp_native_os_close:
    b _close
// Signed i32 offset; do not truncate a successful 64-bit OS position.
// On EOVERFLOW the underlying file cursor may already have moved.
.globl _vpp_native_os_seek
_vpp_native_os_seek:
    str x30, [sp, #-16]!
    sxtw x1, w1
    bl _lseek
    cmp x0, #0
    b.lt 1f
    lsr x9, x0, #31
    cbz x9, 1f
    bl ___error
    mov w9, #84
    str w9, [x0]
    mov w0, #-1
1:
    ldr x30, [sp], #16
    ret
.globl _vpp_native_os_errno
_vpp_native_os_errno:
    str x30, [sp, #-16]!
    bl ___error
    ldr w0, [x0]
    ldr x30, [sp], #16
    ret

// V++ kiểm tra mã tài nguyên và đường dẫn kết thúc NUL. ASM đổi độ lệch thành con trỏ OS đủ 64 bit;
// mkstemp sửa đuôi XXXXXX ngay trong vùng byte của bên gọi.
.globl _vpp_native_os_mkstemp
_vpp_native_os_mkstemp:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    add x0, x9, w0, uxtw
    bl _mkstemp
    TRA_BO_NHO

.globl _vpp_native_os_rename
_vpp_native_os_rename:
    stp x19, x20, [sp, #-32]!
    stp x21, x30, [sp, #16]
    mov w19, w0
    mov w20, w1
    bl _vpp_native_memory_base
    cbz x0, Ldoi_ten_loi
    cmp w19, #0
    b.lt Ldoi_ten_loi
    cmp w20, #0
    b.lt Ldoi_ten_loi
    mov w9, #0x400
    movk w9, #0x810, lsl #16 // Kích thước vùng nhớ trừ 4096
    cmp w19, w9
    b.hi Ldoi_ten_loi
    cmp w20, w9
    b.hi Ldoi_ten_loi
    add x1, x0, w20, uxtw
    add x0, x0, w19, uxtw
    bl _rename
    b Ldoi_ten_tra_ve
Ldoi_ten_loi:
    mov w0, #-1
Ldoi_ten_tra_ve:
    ldp x21, x30, [sp, #16]
    ldp x19, x20, [sp], #32
    ret

