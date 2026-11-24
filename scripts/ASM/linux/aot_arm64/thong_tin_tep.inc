// Đọc định danh tệp và kiểm tra quyền truy cập.

// Linux AArch64 struct stat: dev+0, ino+8, mode+16. Preserve the V++
// Darwin-compatible header (mode+4) and previous mode at +200.
.macro HAM_STAT ten, dung_duong, theo_lien_ket
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
        add x1, x9, w0, uxtw
        mov x0, #-100             // AT_FDCWD
        mov x2, x11
        mov x3, #\theo_lien_ket   // AT_SYMLINK_NOFOLLOW for lstat
        mov x8, #79               // __NR_newfstatat
    .else
        mov x1, x11
        mov x8, #80               // __NR_fstat
    .endif
    svc #0
    bl Llinux_file_syscall_result
    cbnz w0, 2f
    ldr x11, [sp, #24]
    ldr w10, [x11, #4]
    str w10, [x11, #200]
    ldr w10, [x11, #16]
    str w10, [x11, #4]
2:  TRA_BO_NHO
.endm
HAM_STAT vpp_native_os_stat, 1, 0
HAM_STAT vpp_native_os_lstat, 1, 256
HAM_STAT vpp_native_os_fstat, 0, 0
.globl vpp_native_os_access_exec
.globl vpp_native_os_fchmod
vpp_native_os_fchmod:
    mov x8, #52                // __NR_fchmod
    svc #0
    b Llinux_file_syscall_result
vpp_native_os_access_exec:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    add x1, x9, w0, uxtw
    mov x0, #-100              // AT_FDCWD
    mov w2, #1                 // X_OK
    mov x8, #48                // __NR_faccessat
    svc #0
    bl Llinux_file_syscall_result
    TRA_BO_NHO
.globl vpp_native_os_unlink
vpp_native_os_unlink:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    add x1, x9, w0, uxtw
    mov x0, #-100              // AT_FDCWD
    mov x2, #0                 // Remove file or symlink
    mov x8, #35                // __NR_unlinkat
    svc #0
    bl Llinux_file_syscall_result
    TRA_BO_NHO
.globl vpp_native_os_mkdir
vpp_native_os_mkdir:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    add x1, x9, w0, uxtw
    mov x0, #-100              // AT_FDCWD
    mov w2, #448               // mode 0700
    mov x8, #34                // __NR_mkdirat
    svc #0
    bl Llinux_file_syscall_result
    TRA_BO_NHO
