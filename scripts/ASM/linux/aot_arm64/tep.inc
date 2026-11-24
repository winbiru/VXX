// Đọc/ghi, mở/đóng và thao tác tệp.

// Linux AArch64 syscalls return -errno directly. Preserve the libc-style
// errno contract that vpp_native_os_errno exposes to compiled V++ programs.
Llinux_file_syscall_result:
    cmn x0, #4095
    b.cc 1f
    stp x19, x30, [sp, #-16]!
    neg w19, w0
    bl __errno_location
    str w19, [x0]
    mov x0, #-1
    ldp x19, x30, [sp], #16
1:  ret

Llinux_file_read:
    mov x8, #63                   // __NR_read
    svc #0
    b Llinux_file_syscall_result

Llinux_file_write:
    mov x8, #64                   // __NR_write
    svc #0
    b Llinux_file_syscall_result

.macro HAM_NHAP_XUAT ten, dich
    .globl \ten
\ten:
    VAO_BO_NHO
    cmp w1, #0
    b.lt Lbo_nho_loi
    cmp w2, #0
    b.lt Lbo_nho_loi
    uxtw x1, w1
    uxtw x2, w2
    add x10, x1, x2
    mov x11, #0x1400
    movk x11, #0x810, lsl #16
    cmp x10, x11
    b.hi Lbo_nho_loi
    add x1, x9, x1
    bl \dich
    TRA_BO_NHO
.endm
HAM_NHAP_XUAT vpp_native_os_read, Llinux_file_read
HAM_NHAP_XUAT vpp_native_os_write, Llinux_file_write

.globl vpp_native_os_open
vpp_native_os_open:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    add x0, x9, w0, uxtw
// Đổi cờ mở tệp O_WRONLY|O_CREAT|O_TRUNC từ Darwin (1537) sang Linux (577).
    mov w10, #1537
    cmp w1, w10
    b.ne 1f
    mov w1, #577
    b 2f
1:
    cmp w1, #521               // Darwin O_WRONLY|O_CREAT|O_APPEND
    b.ne 2f
    mov w1, #1089              // Linux O_WRONLY|O_CREAT|O_APPEND
2:
    mov w2, #438               // Quyền open(2) Linux: 0666
    mov x3, x2
    mov x2, x1
    mov x1, x0
    mov x0, #-100              // AT_FDCWD
    mov x8, #56                // __NR_openat
    svc #0
    bl Llinux_file_syscall_result
    TRA_BO_NHO
.globl vpp_native_os_close
vpp_native_os_close:
    mov x8, #57                // __NR_close
    svc #0
    b Llinux_file_syscall_result
// Signed i32 offset; do not truncate a successful 64-bit OS position.
// On EOVERFLOW the underlying file cursor may already have moved.
.globl vpp_native_os_seek
vpp_native_os_seek:
    str x30, [sp, #-16]!
    sxtw x1, w1
    mov x8, #62                // __NR_lseek
    svc #0
    bl Llinux_file_syscall_result
    cmp x0, #0
    b.lt 1f
    lsr x9, x0, #31
    cbz x9, 1f
    bl __errno_location
    mov w9, #75
    str w9, [x0]
    mov w0, #-1
1:
    ldr x30, [sp], #16
    ret
.globl vpp_native_os_errno
vpp_native_os_errno:
    str x30, [sp, #-16]!
    bl __errno_location
    ldr w0, [x0]
    ldr x30, [sp], #16
    ret
.globl vpp_native_os_mkstemp
vpp_native_os_mkstemp:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    // mkstemp requires a writable template ending in exactly six X bytes.
    // Use kernel randomness and O_EXCL to avoid races with other creators.
    stp x19, x20, [sp, #-64]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    add x19, x9, w0, uxtw
    mov x20, #0
Llinux_mkstemp_length:
    cmp x20, #4096
    b.hs Llinux_mkstemp_invalid
    ldrb w10, [x19, x20]
    cbz w10, Llinux_mkstemp_end
    add x20, x20, #1
    b Llinux_mkstemp_length
Llinux_mkstemp_end:
    cmp x20, #6
    b.lo Llinux_mkstemp_invalid
    add x21, x19, x20
    sub x21, x21, #6
    mov x10, #0
Llinux_mkstemp_check:
    ldrb w11, [x21, x10]
    cmp w11, #'X'
    b.ne Llinux_mkstemp_invalid
    add x10, x10, #1
    cmp x10, #6
    b.lo Llinux_mkstemp_check
    mov w23, #64
    adrp x24, Llinux_mkstemp_alphabet
    add x24, x24, :lo12:Llinux_mkstemp_alphabet
Llinux_mkstemp_try:
    add x0, sp, #48
    mov x1, #8
    mov x2, #0
    mov x8, #278               // __NR_getrandom
    svc #0
    cmn x0, #4                 // EINTR
    b.eq Llinux_mkstemp_try
    cmp x0, #8
    b.eq Llinux_mkstemp_random
    cmp x0, #0
    b.lt Llinux_mkstemp_result
    mov x0, #-5                // EIO for a short random read
    b Llinux_mkstemp_result
Llinux_mkstemp_random:
    ldr x22, [sp, #48]
    mov x10, x21
    mov x11, #6
    mov x12, #62
Llinux_mkstemp_fill:
    udiv x13, x22, x12
    msub x14, x13, x12, x22
    ldrb w15, [x24, x14]
    strb w15, [x10], #1
    mov x22, x13
    subs x11, x11, #1
    b.ne Llinux_mkstemp_fill
    mov x0, #-100              // AT_FDCWD
    mov x1, x19
    mov x2, #194               // O_RDWR | O_CREAT | O_EXCL
    mov x3, #384               // mode 0600
    mov x8, #56                // __NR_openat
    svc #0
    cmn x0, #17                // EEXIST, choose a new name
    b.ne Llinux_mkstemp_result
    subs w23, w23, #1
    b.ne Llinux_mkstemp_try
    mov x0, #-17
    b Llinux_mkstemp_result
Llinux_mkstemp_invalid:
    mov x0, #-22               // EINVAL
Llinux_mkstemp_result:
    ldp x23, x24, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #64
    bl Llinux_file_syscall_result
    TRA_BO_NHO
.globl vpp_native_os_rename
vpp_native_os_rename:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    mov w12, w0
    mov w0, w1
    KIEM_TRA_BIEN 4096
    add x3, x9, w1, uxtw       // newpath
    add x1, x9, w12, uxtw      // oldpath
    mov x0, #-100              // AT_FDCWD
    mov x2, #-100              // AT_FDCWD
    mov x8, #38                // __NR_renameat
    svc #0
    bl Llinux_file_syscall_result
    TRA_BO_NHO

.pushsection .rodata
Llinux_mkstemp_alphabet:
    .ascii "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
.popsection

