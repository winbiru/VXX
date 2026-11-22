// Tìm và chép giá trị môi trường trong giới hạn vùng nhớ.

// Return a pointer to the value of NAME, or zero when absent. The variable
// table is supplied by the process ABI; no libc getenv call is required.
// The name must terminate within the V++ 4096-byte path/name window.
Llinux_env_lookup:
    adrp x11, :got:environ
    ldr x11, [x11, :got_lo12:environ]
    ldr x11, [x11]
    cbz x11, 4f
    ldrb w12, [x0]
    cbz w12, 4f
1:  ldr x12, [x11], #8
    cbz x12, 4f
    mov x13, x0
    mov x14, x12
    mov w15, #4096
2:  ldrb w16, [x13], #1
    cbz w16, 3f
    cmp w16, #'='
    b.eq 1b
    subs w15, w15, #1
    b.eq 1b
    ldrb w17, [x14], #1
    cmp w16, w17
    b.eq 2b
    b 1b
3:  ldrb w16, [x14]
    cmp w16, #'='
    b.ne 1b
    add x0, x14, #1
    ret
4:  mov x0, #0
    ret

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
    bl Llinux_env_lookup
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

