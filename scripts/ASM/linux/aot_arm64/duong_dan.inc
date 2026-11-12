// Lấy đường chương trình, thư mục hiện tại và thư mục nhà.

.globl vpp_native_os_executable_path
vpp_native_os_executable_path:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    add x1, x9, w0, uxtw
    str x1, [sp, #24]
    adrp x0, Lchuong_trinh_duong
    add x0, x0, :lo12:Lchuong_trinh_duong
    mov x2, #4095
    bl readlink
    cmp x0, #0
    b.lt Lbo_nho_loi
    cmp x0, #4095
    b.hs Lbo_nho_loi
    ldr x1, [sp, #24]
    strb wzr, [x1, x0]
    TRA_BO_NHO
.globl vpp_native_os_home
vpp_native_os_home:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    add x11, x9, w0, uxtw
    str x11, [sp, #24]
    adrp x0, Lthu_muc_nha_khoa
    add x0, x0, :lo12:Lthu_muc_nha_khoa
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
.globl vpp_native_os_cwd
vpp_native_os_cwd:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    add x0, x9, w0, uxtw
    str x0, [sp, #24]
    mov x1, #4096
    bl getcwd
    cbz x0, Lbo_nho_loi
    ldr x11, [sp, #24]
    mov w9, #0
1:  cmp w9, #4096
    b.hs Lbo_nho_loi
    ldrb w10, [x11, x9]
    cbz w10, 2f
    add w9, w9, #1
    b 1b
2:  mov w0, w9
    TRA_BO_NHO

