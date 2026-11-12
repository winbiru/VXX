// Đọc định danh tệp và kiểm tra quyền truy cập.

// struct stat glibc Linux ARM64: dev+0, ino+8, mode+16. Chép mode sang +4 cho bố trí Darwin mà
// runtime.vi đang đọc.
.macro HAM_STAT ten, dich, dung_duong
    .globl \ten
\ten:
    VAO_BO_NHO
    cmp w1, #3376
    b.eq 1f
    cmp w1, #3584
    b.ne Lbo_nho_loi
1:  add x11, x9, w1, uxtw
    str x11, [sp, #24]
    .if \dung_duong
        KIEM_TRA_BIEN 4096
        add x0, x9, w0, uxtw
    .endif
    mov x1, x11
    bl \dich
    cbnz w0, 2f
    ldr x11, [sp, #24]
    ldr w10, [x11, #16]
    str w10, [x11, #4]
2:  TRA_BO_NHO
.endm
HAM_STAT vpp_native_os_stat, stat, 1
HAM_STAT vpp_native_os_lstat, lstat, 1
HAM_STAT vpp_native_os_fstat, fstat, 0
.globl vpp_native_os_access_exec
.globl vpp_native_os_fchmod
vpp_native_os_fchmod:
    b fchmod
vpp_native_os_access_exec:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    add x0, x9, w0, uxtw
    mov w1, #1
    bl access
    TRA_BO_NHO
.globl vpp_native_os_unlink
vpp_native_os_unlink:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    add x0, x9, w0, uxtw
    bl unlink
    TRA_BO_NHO
.globl vpp_native_os_mkdir
vpp_native_os_mkdir:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    add x0, x9, w0, uxtw
    mov w1, #448
    bl mkdir
    TRA_BO_NHO
