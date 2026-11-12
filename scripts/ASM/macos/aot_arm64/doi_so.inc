// Kiểm tra chỉ số, lấy độ dài và đọc byte đối số chương trình.

.globl _vpp_native_argc
_vpp_native_argc:
    str x30, [sp, #-16]!
    bl __NSGetArgc
    ldr w0, [x0]
    ldr x30, [sp], #16
    ret

// Con trỏ argv giữ đủ 64 bit trong ranh giới máy.
Ldoi_so_con_tro:
    stp x0, x30, [sp, #-16]!
    bl __NSGetArgc
    ldr w9, [x0]
    ldr w10, [sp]
    cmp w10, #0
    b.lt Ldoi_so_khong_hop_le
    cmp w10, w9
    b.ge Ldoi_so_khong_hop_le
    bl __NSGetArgv
    ldr x0, [x0]
    ldr w10, [sp]
    ldr x0, [x0, w10, uxtw #3]
    ldr x30, [sp, #8]
    add sp, sp, #16
    ret
Ldoi_so_khong_hop_le:
    mov x0, #0
    ldr x30, [sp, #8]
    add sp, sp, #16
    ret
.globl _vpp_native_os_argument_length
_vpp_native_os_argument_length:
    str x30, [sp, #-16]!
    bl Ldoi_so_con_tro
    cbz x0, Ldoi_so_do_dai_loi
    mov x9, #0
    mov x11, #0x100000
Ldoi_so_do_dai_lap:
    cmp x9, x11
    b.hi Ldoi_so_do_dai_loi
    ldrb w10, [x0, x9]
    cbz w10, Ldoi_so_do_dai_xong
    add x9, x9, #1
    b Ldoi_so_do_dai_lap
Ldoi_so_do_dai_xong:
    mov w0, w9
    ldr x30, [sp], #16
    ret
Ldoi_so_do_dai_loi:
    mov w0, #-1
    ldr x30, [sp], #16
    ret
.globl _vpp_native_os_argument_byte
_vpp_native_os_argument_byte:
    stp x1, x30, [sp, #-16]!
    bl Ldoi_so_con_tro
    cbz x0, Ldoi_so_byte_loi
    ldr w1, [sp]
    cmp w1, #0
    b.lt Ldoi_so_byte_loi
    mov w11, #0x100000
    cmp w1, w11
    b.hs Ldoi_so_byte_loi
    mov x9, #0
Ldoi_so_byte_lap:
    ldrb w10, [x0, x9]
    cbz w10, Ldoi_so_byte_loi
    cmp w9, w1
    b.eq Ldoi_so_byte_xong
    add x9, x9, #1
    b Ldoi_so_byte_lap
Ldoi_so_byte_xong:
    mov w0, w10
    ldr x30, [sp, #8]
    add sp, sp, #16
    ret
Ldoi_so_byte_loi:
    mov w0, #-1
    ldr x30, [sp, #8]
    add sp, sp, #16
    ret

