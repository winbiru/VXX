// Tìm và chép giá trị môi trường trong giới hạn vùng nhớ.

.globl vpp_native_os_env_copy
vpp_native_os_env_copy:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    mov w12, w0
    mov w0, w1
    KIEM_TRA_BIEN 4096
    add x10, x9, w1, uxtw
    str x10, [sp, #24]
    add x0, x9, w12, uxtw
    bl getenv
    cbz x0, Lbo_nho_loi
    ldr x11, [sp, #24]
    mov w9, #0
1:  ldrb w10, [x0, x9]
    cbz w10, 2f
    cmp w9, #4095
    b.hs Lbo_nho_loi
    strb w10, [x11, x9]
    add w9, w9, #1
    b 1b
2:  strb wzr, [x11, x9]
    mov w0, w9
    TRA_BO_NHO

