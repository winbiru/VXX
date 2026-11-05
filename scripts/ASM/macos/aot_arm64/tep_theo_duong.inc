// Khung xử lý đường dẫn, thông tin mô tả tệp và tạo/xóa thư mục.

// Ghi đường dẫn OS vào ô tối đa 4096 byte kể cả NUL. V++ sở hữu mã tài nguyên và ghi độ dài; con trỏ
// giữ đủ 64 bit trong vùng nhớ bộ thực thi.
.macro VAO_DUONG
    stp x19, x20, [sp, #-32]!
    stp x21, x30, [sp, #16]
    mov w19, w0
    bl _vpp_native_memory_base
    cbz x0, Lduong_loi
    cmp w19, #0
    b.lt Lduong_loi
    mov w9, #0x400
    movk w9, #0x810, lsl #16 // Kích thước vùng nhớ trừ 4096
    cmp w19, w9
    b.hi Lduong_loi
    add x20, x0, w19, uxtw
.endm
.macro TRA_DUONG
    ldp x21, x30, [sp, #16]
    ldp x19, x20, [sp], #32
    ret
.endm
// fstat nhận mô tả tệp và một trong hai vùng struct stat dành riêng.
.globl _vpp_native_os_fstat
_vpp_native_os_fstat:
    VAO_BO_NHO
    cmp w1, #3376
    b.eq Lstat_mo_ta_da_kiem_tra
    cmp w1, #3584
    b.ne Lstat_mo_ta_loi
Lstat_mo_ta_da_kiem_tra:
    add x1, x9, w1, uxtw
    bl _fstat
    TRA_BO_NHO
Lstat_mo_ta_loi:
    mov w0, #-1
    TRA_BO_NHO

// unlink, mkdir và getenv nhận độ lệch vùng nhớ.
.globl _vpp_native_os_unlink
_vpp_native_os_unlink:
    VAO_DUONG
    mov x0, x20
    bl _unlink
    TRA_DUONG

.globl _vpp_native_os_mkdir
_vpp_native_os_mkdir:
    VAO_DUONG
    mov x0, x20
    mov w1, #448            // Quyền 0700
    bl _mkdir
    TRA_DUONG

