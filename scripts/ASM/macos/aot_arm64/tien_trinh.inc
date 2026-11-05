// Tạo, thăm dò, đọc, đợi và đóng tiến trình theo ABI của nền tảng.

// Trình liên kết V++ ghi con trỏ argv đủ độ rộng vào bảng riêng 4096 mục sau các ô dữ liệu. argv[0]
// là /usr/bin/clang hoặc /usr/bin/ld; ASM chuyển bảng này vào posix_spawn.
.globl _vpp_native_os_spawn_clang
_vpp_native_os_spawn_clang:
    VAO_BO_NHO
    mov w10, #0x1400
    movk w10, #0x800, lsl #16
    cmp w0, w10
    b.ne Ltao_tien_trinh_loi
    add x10, x9, w0, uxtw
    ldr x11, [x10]
    cbz x11, Ltao_tien_trinh_loi
// Giữ argv qua _NSGetEnviron vì ABI C có thể ghi đè x4.
    sub sp, sp, #32
    str x10, [sp]
    bl __NSGetEnviron
    cbz x0, Ltao_tien_trinh_da_loi
    ldr x5, [x0]
    cbz x5, Ltao_tien_trinh_da_loi
    ldr x4, [sp]
    add x0, sp, #16
    ldr x1, [x4]         // Đường chương trình posix_spawn = argv[0]
    cbz x1, Ltao_tien_trinh_da_loi
    mov x2, #0
    mov x3, #0
    bl _posix_spawn
    cbnz w0, Ltao_tien_trinh_da_loi
    ldr w0, [sp, #16]
    add sp, sp, #32
    TRA_BO_NHO
Ltao_tien_trinh_da_loi:
    add sp, sp, #32
Ltao_tien_trinh_loi:
    mov w0, #-1
    TRA_BO_NHO

.globl _vpp_native_os_waitpid
_vpp_native_os_waitpid:
    VAO_BO_NHO
    cmp w0, #1
    b.lt Ldoi_loi
    cmp w1, #0
    b.lt Ldoi_loi
    mov w10, #(0x1400 - 4)
    movk w10, #0x810, lsl #16
    cmp w1, w10
    b.hi Ldoi_loi
    add x1, x9, w1, uxtw
    mov w2, #0
    bl _waitpid
    TRA_BO_NHO
Ldoi_loi:
    mov w0, #-1
    TRA_BO_NHO

