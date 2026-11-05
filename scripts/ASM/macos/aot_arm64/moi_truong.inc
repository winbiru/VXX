// Tìm và chép giá trị môi trường trong giới hạn vùng nhớ.

.globl _vpp_native_os_env_copy
_vpp_native_os_env_copy:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    mov w12, w0
    mov w0, w1
    KIEM_TRA_BIEN 4096
    add x10, x9, w1, uxtw
    str x10, [sp, #24]       // Ô trống trong khung VAO_BO_NHO
    add x0, x9, w12, uxtw
    bl _getenv
    cbz x0, Lmoi_truong_loi
    ldr x11, [sp, #24]
    mov w9, #0
Lmoi_truong_chep:
    ldrb w10, [x0, x9]
    cbz w10, Lmoi_truong_xong
    cmp w9, #4095
    b.hs Lmoi_truong_loi
    strb w10, [x11, x9]
    add w9, w9, #1
    b Lmoi_truong_chep
Lmoi_truong_xong:
    strb wzr, [x11, x9]
    mov w0, w9
    TRA_BO_NHO
Lmoi_truong_loi:
    mov w0, #-1
    TRA_BO_NHO

