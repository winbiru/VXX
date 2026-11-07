// Lấy đường chương trình, thư mục hiện tại và thư mục nhà.

.globl _vpp_native_os_executable_path
_vpp_native_os_executable_path:
    VAO_DUONG
    sub sp, sp, #16
    mov w9, #4096
    str w9, [sp]
    mov x0, x20
    mov x1, sp
    bl __NSGetExecutablePath
    cbnz w0, Lchuong_trinh_loi
    mov w9, #0
Lchuong_trinh_do_dai:
    ldrb w10, [x20, x9]
    cbz w10, Lchuong_trinh_xong
    add w9, w9, #1
    cmp w9, #4095
    b.hi Lchuong_trinh_loi
    b Lchuong_trinh_do_dai
Lchuong_trinh_xong:
    mov w0, w9
    add sp, sp, #16
    TRA_DUONG
Lchuong_trinh_loi:
    add sp, sp, #16
    b Lduong_loi

.globl _vpp_native_os_home
_vpp_native_os_home:
    VAO_DUONG
    adrp x0, Lthu_muc_nha_khoa@PAGE
    add x0, x0, Lthu_muc_nha_khoa@PAGEOFF
    bl _getenv
    cbz x0, Lduong_loi
    mov x21, x0
    mov w9, #0
Lthu_muc_nha_chep:
    ldrb w10, [x21, x9]
    cbz w10, Lthu_muc_nha_xong
    cmp w9, #4095
    b.hs Lduong_loi
    strb w10, [x20, x9]
    add x9, x9, #1
    b Lthu_muc_nha_chep
Lthu_muc_nha_xong:
    strb wzr, [x20, x9]
    mov w0, w9
    TRA_DUONG
.globl _vpp_native_os_cwd
_vpp_native_os_cwd:
    VAO_DUONG
    mov x0, x20
    mov x1, #4096
    bl _getcwd
    cbz x0, Lduong_loi
    mov w9, #0
Lthu_muc_hien_tai_do_dai:
    ldrb w10, [x20, x9]
    cbz w10, Lthu_muc_hien_tai_xong
    add x9, x9, #1
    cmp w9, #4096
    b.hs Lduong_loi
    b Lthu_muc_hien_tai_do_dai
Lthu_muc_hien_tai_xong:
    mov w0, w9
    TRA_DUONG
Lduong_loi:
    mov w0, #-1
    TRA_DUONG

.section __TEXT,__cstring,cstring_literals
Lthu_muc_nha_khoa:
    .asciz "VPP_HOME"
.text
