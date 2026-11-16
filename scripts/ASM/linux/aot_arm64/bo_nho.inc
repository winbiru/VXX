// Cấp vùng nhớ, kiểm tra giới hạn và thao tác byte/i32/con trỏ.

.text
.p2align 2
.globl vpp_native_memory_base
vpp_native_memory_base:
    adrp x9, :got:vpp_native_arena_base
    ldr x9, [x9, :got_lo12:vpp_native_arena_base]
    ldr x0, [x9]
    cbnz x0, 1f
    str x30, [sp, #-16]!
    mov x0, #0
    mov x1, #0x1400
    movk x1, #0x810, lsl #16  // 135271424 byte, 64 byte buffers at 2 MiB each
    mov x2, #3               // PROT_READ | PROT_WRITE
    mov x3, #0x22            // Linux MAP_PRIVATE | MAP_ANONYMOUS
    mov x4, #-1
    mov x5, #0
    bl mmap
    cmn x0, #1
    b.eq 2f
    adrp x9, :got:vpp_native_arena_base
    ldr x9, [x9, :got_lo12:vpp_native_arena_base]
    str x0, [x9]
    ldr x30, [sp], #16
1:  ret
2:  mov x0, #0
    ldr x30, [sp], #16
    ret

.macro VAO_BO_NHO
    stp x0, x1, [sp, #-32]!
    stp x30, x2, [sp, #16]
    // Arena đã có: lấy địa chỉ trực tiếp, không gọi hàm cho mỗi lần đọc/ghi.
    // Giữ nguyên khung 32 byte vì các primitive còn dùng vùng tạm trên stack.
    adrp x9, :got:vpp_native_arena_base
    ldr x9, [x9, :got_lo12:vpp_native_arena_base]
    ldr x9, [x9]
    cbnz x9, 1f
    bl vpp_native_memory_base
    mov x9, x0
1:
    ldp x0, x1, [sp]
    ldr x2, [sp, #24]
    cbz x9, Lbo_nho_loi
.endm
.macro KIEM_TRA_BIEN do_rong
    cmp w0, #0
    b.lt Lbo_nho_loi
    mov w10, #(0x1400 - \do_rong)
    movk w10, #0x810, lsl #16
    cmp w0, w10
    b.hi Lbo_nho_loi
.endm
.macro TRA_BO_NHO
    ldr x30, [sp, #16]
    add sp, sp, #32
    ret
.endm
.globl vpp_native_memory_read_byte
vpp_native_memory_read_byte:
    VAO_BO_NHO
    KIEM_TRA_BIEN 1
    ldrb w0, [x9, w0, uxtw]
    TRA_BO_NHO
.globl vpp_native_memory_write_byte
vpp_native_memory_write_byte:
    VAO_BO_NHO
    KIEM_TRA_BIEN 1
    cmp w1, #255
    b.hi Lbo_nho_loi
    strb w1, [x9, w0, uxtw]
    mov w0, #0
    TRA_BO_NHO
.globl vpp_native_memory_read_i32
vpp_native_memory_read_i32:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4
    ldr w0, [x9, w0, uxtw]
    TRA_BO_NHO
.globl vpp_native_memory_write_i32
vpp_native_memory_write_i32:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4
    str w1, [x9, w0, uxtw]
    mov w0, #0
    TRA_BO_NHO
// Xóa [lệch, lệch + dài), kiểm tra độ dài và giới hạn vùng nhớ.
.globl vpp_native_memory_zero
vpp_native_memory_zero:
    VAO_BO_NHO
    cmp w1, #0
    b.lt Lbo_nho_loi
    mov w10, #0x1400
    movk w10, #0x810, lsl #16
    subs w10, w10, w1
    b.lo Lbo_nho_loi
    cmp w0, w10
    b.hi Lbo_nho_loi
    add x11, x9, w0, uxtw
    lsr w12, w1, #4
    cbz w12, 3f
2:  stp xzr, xzr, [x11], #16
    subs w12, w12, #1
    b.ne 2b
3:  and w12, w1, #15
    cbz w12, 4f
2:  strb wzr, [x11], #1
    subs w12, w12, #1
    b.ne 2b
4:  mov w0, #0
    TRA_BO_NHO
.globl vpp_native_memory_bind
vpp_native_memory_bind:
    VAO_BO_NHO
    KIEM_TRA_BIEN 8
    tst w0, #7
    b.ne Lbo_nho_loi
    cmp w1, #0
    b.lt Lbo_nho_loi
    mov w10, #0x1400
    movk w10, #0x810, lsl #16
    cmp w1, w10
    b.hs Lbo_nho_loi
    add x1, x9, w1, uxtw
    str x1, [x9, w0, uxtw]
    mov w0, #0
    TRA_BO_NHO
Lbo_nho_loi:
    mov w0, #-1
    TRA_BO_NHO


// CP8: checked arena memmove and ASCII prefix (NEON).
// The caller supplies signed i32 arena offsets and lengths.
.globl vpp_native_memory_copy
vpp_native_memory_copy:
    VAO_BO_NHO
    cmp w2, #0
    b.lt Lbo_nho_loi
    mov w10, #0x1400
    movk w10, #0x810, lsl #16
    subs w10, w10, w2
    b.lo Lbo_nho_loi
    cmp w0, w10
    b.hi Lbo_nho_loi
    cmp w1, w10
    b.hi Lbo_nho_loi
    cbz w2, Lbulk_copy_done
    cmp w0, w1
    b.eq Lbulk_copy_done
    add x11, x9, w0, uxtw
    add x12, x9, w1, uxtw
    b.lo Lbulk_copy_forward
    sub w10, w0, w1
    cmp w10, w2
    b.hs Lbulk_copy_forward
    add x11, x11, w2, uxtw
    add x12, x12, w2, uxtw
Lbulk_copy_backward:
    cmp w2, #16
    b.lo Lbulk_copy_backward_tail
    ldr q0, [x12, #-16]!
    str q0, [x11, #-16]!
    sub w2, w2, #16
    b Lbulk_copy_backward
Lbulk_copy_backward_tail:
    cbz w2, Lbulk_copy_done
    ldrb w10, [x12, #-1]!
    strb w10, [x11, #-1]!
    sub w2, w2, #1
    b Lbulk_copy_backward_tail
Lbulk_copy_forward:
    cmp w2, #16
    b.lo Lbulk_copy_forward_tail
    ldr q0, [x12], #16
    str q0, [x11], #16
    sub w2, w2, #16
    b Lbulk_copy_forward
Lbulk_copy_forward_tail:
    cbz w2, Lbulk_copy_done
    ldrb w10, [x12], #1
    strb w10, [x11], #1
    sub w2, w2, #1
    b Lbulk_copy_forward_tail
Lbulk_copy_done:
    mov w0, #0
    TRA_BO_NHO

.globl vpp_native_memory_ascii_prefix
vpp_native_memory_ascii_prefix:
    VAO_BO_NHO
    cmp w1, #0
    b.lt Lbo_nho_loi
    mov w10, #0x1400
    movk w10, #0x810, lsl #16
    subs w10, w10, w1
    b.lo Lbo_nho_loi
    cmp w0, w10
    b.hi Lbo_nho_loi
    add x11, x9, w0, uxtw
    mov w0, #0
Lbulk_ascii_vector:
    cmp w1, #16
    b.lo Lbulk_ascii_tail
    ldr q0, [x11]
    umaxv b1, v0.16b
    umov w10, v1.b[0]
    tbnz w10, #7, Lbulk_ascii_tail
    add x11, x11, #16
    add w0, w0, #16
    sub w1, w1, #16
    b Lbulk_ascii_vector
Lbulk_ascii_tail:
    cbz w1, Lbulk_ascii_done
    ldrb w10, [x11], #1
    tbnz w10, #7, Lbulk_ascii_done
    add w0, w0, #1
    sub w1, w1, #1
    b Lbulk_ascii_tail
Lbulk_ascii_done:
    TRA_BO_NHO
