// Cấp vùng nhớ, kiểm tra giới hạn và thao tác byte/i32/con trỏ.

.text
.p2align 2
.globl _vpp_native_memory_base
_vpp_native_memory_base:
    adrp x9, _vpp_native_arena_base@PAGE
    ldr x0, [x9, _vpp_native_arena_base@PAGEOFF]
    cbnz x0, 1f
    str x30, [sp, #-16]!
    mov x0, #0
    mov x1, #0x1400
    movk x1, #0x810, lsl #16  // 135271424 byte
    mov x2, #3               // PROT_READ | PROT_WRITE, chỉ đọc/ghi
    mov x3, #0x1002          // MAP_PRIVATE | MAP_ANON
    mov x4, #-1
    mov x5, #0
    bl _mmap
    cmn x0, #1
    b.eq 2f
    adrp x9, _vpp_native_arena_base@PAGE
    str x0, [x9, _vpp_native_arena_base@PAGEOFF]
    ldr x30, [sp], #16
1:  ret
2:  mov x0, #0
    ldr x30, [sp], #16
    ret

.macro VAO_BO_NHO
    // Các primitive cùng macro dùng [sp, #24] làm bộ nhớ tạm.
    // Giữ khung 32 byte, chỉ bỏ lời gọi hàm khi arena đã được khởi tạo.
    stp x0, x1, [sp, #-32]!
    str x30, [sp, #16]
    adrp x9, _vpp_native_arena_base@PAGE
    ldr x9, [x9, _vpp_native_arena_base@PAGEOFF]
    cbnz x9, 1f
    bl _vpp_native_memory_base
    mov x9, x0
1:
    ldp x0, x1, [sp]
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

.globl _vpp_native_memory_read_byte
_vpp_native_memory_read_byte:
    VAO_BO_NHO
    KIEM_TRA_BIEN 1
    ldrb w0, [x9, w0, uxtw]
    TRA_BO_NHO
.globl _vpp_native_memory_write_byte
_vpp_native_memory_write_byte:
    VAO_BO_NHO
    KIEM_TRA_BIEN 1
    cmp w1, #255
    b.hi Lbo_nho_loi
    strb w1, [x9, w0, uxtw]
    mov w0, #0
    TRA_BO_NHO
.globl _vpp_native_memory_read_i32
_vpp_native_memory_read_i32:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4
    ldr w0, [x9, w0, uxtw]
    TRA_BO_NHO
.globl _vpp_native_memory_write_i32
_vpp_native_memory_write_i32:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4
    str w1, [x9, w0, uxtw]
    mov w0, #0
    TRA_BO_NHO
// Xóa [lệch, lệch + dài), từ chối độ dài âm và tràn biên arena.
.globl _vpp_native_memory_zero
_vpp_native_memory_zero:
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
.globl _vpp_native_memory_bind
_vpp_native_memory_bind:
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
    str x1, [x9, w0, uxtw]  // Lưu đủ 64 bit của con trỏ
    mov w0, #0
    TRA_BO_NHO
Lbo_nho_loi:
    mov w0, #-1
    TRA_BO_NHO

