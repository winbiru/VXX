// Khởi tạo, cập nhật và kết thúc phép băm SHA-256.

// SHA-256 từng khối dùng CC_SHA256_CTX 104 byte trong trang siêu dữ liệu.
.globl _vpp_native_sha_begin
_vpp_native_sha_begin:
    VAO_BO_NHO
    add x0, x9, #3904
    bl _CC_SHA256_Init
    cmp w0, #1
    cset w0, ne
    neg w0, w0
    TRA_BO_NHO

.globl _vpp_native_sha_update
_vpp_native_sha_update:
    VAO_BO_NHO
    cmp w0, #0
    b.lt Lsha_loi
    cmp w1, #0
    b.lt Lsha_loi
    uxtw x10, w0
    uxtw x11, w1
    add x10, x10, x11
    mov x11, #0x1400
    movk x11, #0x810, lsl #16
    cmp x10, x11
    b.hi Lsha_loi
    mov w2, w1
    add x1, x9, w0, uxtw
    add x0, x9, #3904
    bl _CC_SHA256_Update
    cmp w0, #1
    cset w0, ne
    neg w0, w0
    TRA_BO_NHO

.globl _vpp_native_sha_end
_vpp_native_sha_end:
    VAO_BO_NHO
    KIEM_TRA_BIEN 32
    add x1, x9, #3904
    add x0, x9, w0, uxtw
    bl _CC_SHA256_Final
    cmp w0, #1
    cset w0, ne
    neg w0, w0
    TRA_BO_NHO
Lsha_loi:
    mov w0, #-1
    TRA_BO_NHO

