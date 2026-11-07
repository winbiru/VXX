// Tạo, thăm dò, đọc, đợi và đóng tiến trình theo ABI của nền tảng.

.globl vpp_native_os_spawn_clang
vpp_native_os_spawn_clang:
    VAO_BO_NHO
    mov w10, #0x1400
    movk w10, #0x400, lsl #16
    cmp w0, w10
    b.eq Lclang_argv_offset_valid
    movk w10, #0x800, lsl #16
    cmp w0, w10
    b.ne Lbo_nho_loi
Lclang_argv_offset_valid:
    add x4, x9, w0, uxtw
    ldr x10, [x4]
    cbz x10, Lbo_nho_loi
    adrp x5, :got:environ
    ldr x5, [x5, :got_lo12:environ]
    ldr x5, [x5]
    cbz x5, Lbo_nho_loi
    add x0, sp, #24           // Đầu ra pid_t tại sp[24..27] còn trống
    adrp x1, Lclang_duong
    add x1, x1, :lo12:Lclang_duong
    mov x2, #0
    mov x3, #0
    bl posix_spawn
    cbnz w0, Lbo_nho_loi
    ldr w0, [sp, #24]
    TRA_BO_NHO
.globl vpp_native_os_waitpid
vpp_native_os_waitpid:
    VAO_BO_NHO
    cmp w0, #1
    b.lt Lbo_nho_loi
    mov w12, w0
    mov w0, w1
    KIEM_TRA_BIEN 4
    mov w0, w12
    add x1, x9, w1, uxtw
    mov w2, #0
    bl waitpid
    TRA_BO_NHO
