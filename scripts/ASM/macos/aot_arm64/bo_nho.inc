// Cấp vùng nhớ, kiểm tra giới hạn và thao tác byte/i32/con trỏ.

.text
.p2align 2
.globl _vpp_native_memory_base
_vpp_native_memory_base:
    // Single-threaded bootstrap keeps the original fast path. Once workers
    // are enabled the shared pointer stays null; each OS thread owns a key.
    adrp x9, _vpp_native_arena_base@PAGE
    ldr x0, [x9, _vpp_native_arena_base@PAGEOFF]
    cbnz x0, Lmemory_base_return
    stp x19, x30, [sp, #-16]!
    adrp x9, _vpp_native_arena_tls_enabled@PAGE
    ldr w19, [x9, _vpp_native_arena_tls_enabled@PAGEOFF]
    cbz w19, Lmemory_base_allocate
    adrp x9, _vpp_native_arena_tls_key@PAGE
    ldr x0, [x9, _vpp_native_arena_tls_key@PAGEOFF]
    bl _pthread_getspecific
    cbnz x0, Lmemory_base_pop
Lmemory_base_allocate:
    mov x0, #0
    mov x1, #0x1400
    movk x1, #0x810, lsl #16  // 135271424 byte
    mov x2, #3               // PROT_READ | PROT_WRITE
    mov x3, #0x1002          // MAP_PRIVATE | MAP_ANON
    mov x4, #-1
    mov x5, #0
    bl _mmap
    cmn x0, #1
    b.eq Lmemory_base_failed
    cbnz w19, Lmemory_base_set_tls
    adrp x9, _vpp_native_arena_base@PAGE
    str x0, [x9, _vpp_native_arena_base@PAGEOFF]
    b Lmemory_base_pop
Lmemory_base_set_tls:
    mov x19, x0
    mov x1, x0
    adrp x9, _vpp_native_arena_tls_key@PAGE
    ldr x0, [x9, _vpp_native_arena_tls_key@PAGEOFF]
    bl _pthread_setspecific
    cbnz w0, Lmemory_base_release_failed
    mov x0, x19
    b Lmemory_base_pop
Lmemory_base_release_failed:
    mov x0, x19
    mov x1, #0x1400
    movk x1, #0x810, lsl #16
    bl _munmap
Lmemory_base_failed:
    mov x0, #0
Lmemory_base_pop:
    ldp x19, x30, [sp], #16
Lmemory_base_return:
    ret

// Observe the current arena without allocating (JIT leaf validation).
.globl _vpp_native_memory_current
_vpp_native_memory_current:
    adrp x9, _vpp_native_arena_tls_enabled@PAGE
    ldr w9, [x9, _vpp_native_arena_tls_enabled@PAGEOFF]
    cbnz w9, Larena_current_tls
    adrp x9, _vpp_native_arena_base@PAGE
    ldr x0, [x9, _vpp_native_arena_base@PAGEOFF]
    ret
Larena_current_tls:
    adrp x9, _vpp_native_arena_tls_key@PAGE
    ldr x0, [x9, _vpp_native_arena_tls_key@PAGEOFF]
    b _pthread_getspecific

// Enable exactly once, on the root thread before creating any workers.
// Transfer the existing arena into TLS without changing its address. Worker
// entry/exit owns its arena explicitly; the key has no implicit destructor.
.globl _vpp_native_arena_enable_threads
_vpp_native_arena_enable_threads:
    adrp x9, _vpp_native_arena_tls_enabled@PAGE
    ldr w0, [x9, _vpp_native_arena_tls_enabled@PAGEOFF]
    cbnz w0, Larena_enable_done
    stp x19, x30, [sp, #-16]!
    bl _vpp_native_memory_base
    cbz x0, Larena_enable_failed
    mov x19, x0
    adrp x0, _vpp_native_arena_tls_key@PAGE
    add x0, x0, _vpp_native_arena_tls_key@PAGEOFF
    mov x1, #0
    bl _pthread_key_create
    cbnz w0, Larena_enable_failed
    adrp x9, _vpp_native_arena_tls_key@PAGE
    ldr x0, [x9, _vpp_native_arena_tls_key@PAGEOFF]
    mov x1, x19
    bl _pthread_setspecific
    cbnz w0, Larena_enable_delete
    adrp x0, _vpp_native_worker_tls_key@PAGE
    add x0, x0, _vpp_native_worker_tls_key@PAGEOFF
    mov x1, #0
    bl _pthread_key_create
    cbnz w0, Larena_enable_delete
    adrp x9, _vpp_native_arena_tls_enabled@PAGE
    mov w10, #1
    str w10, [x9, _vpp_native_arena_tls_enabled@PAGEOFF]
    adrp x9, _vpp_native_arena_base@PAGE
    str xzr, [x9, _vpp_native_arena_base@PAGEOFF]
    ldp x19, x30, [sp], #16
Larena_enable_done:
    mov w0, #0
    ret
Larena_enable_delete:
    adrp x9, _vpp_native_arena_tls_key@PAGE
    ldr x0, [x9, _vpp_native_arena_tls_key@PAGEOFF]
    bl _pthread_key_delete
Larena_enable_failed:
    ldp x19, x30, [sp], #16
    mov w0, #-1
    ret

.macro VAO_BO_NHO
    // Các primitive cùng macro dùng [sp, #24] làm bộ nhớ tạm.
    // Giữ khung 32 byte, chỉ bỏ lời gọi hàm khi arena đã được khởi tạo.
    stp x0, x1, [sp, #-32]!
    stp x30, x2, [sp, #16]
    adrp x9, _vpp_native_arena_base@PAGE
    ldr x9, [x9, _vpp_native_arena_base@PAGEOFF]
    cbnz x9, 1f
    bl _vpp_native_memory_base
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

.macro VAO_BO_NHO_LA
    adrp x9, _vpp_native_arena_base@PAGE
    ldr x9, [x9, _vpp_native_arena_base@PAGEOFF]
    cbnz x9, 1f
    stp x0, x1, [sp, #-32]!
    stp x30, x2, [sp, #16]
    bl _vpp_native_memory_base
    mov x9, x0
    ldp x30, x2, [sp, #16]
    ldp x0, x1, [sp], #32
    cbz x9, Lbo_nho_la_loi
1:
.endm
.macro KIEM_TRA_BIEN_LA do_rong
    // Unsigned comparison rejects negative i32 offsets as well.
    mov w10, #(0x1400 - \do_rong)
    movk w10, #0x810, lsl #16
    cmp w0, w10
    b.hi Lbo_nho_la_loi
.endm

.globl _vpp_native_memory_read_byte
_vpp_native_memory_read_byte:
    VAO_BO_NHO_LA
    KIEM_TRA_BIEN_LA 1
    ldrb w0, [x9, w0, uxtw]
    ret
.globl _vpp_native_memory_write_byte
_vpp_native_memory_write_byte:
    VAO_BO_NHO_LA
    KIEM_TRA_BIEN_LA 1
    cmp w1, #255
    b.hi Lbo_nho_la_loi
    strb w1, [x9, w0, uxtw]
    mov w0, #0
    ret
.globl _vpp_native_memory_read_i32
_vpp_native_memory_read_i32:
    VAO_BO_NHO_LA
    KIEM_TRA_BIEN_LA 4
    ldr w0, [x9, w0, uxtw]
    ret
.globl _vpp_native_memory_write_i32
_vpp_native_memory_write_i32:
    VAO_BO_NHO_LA
    KIEM_TRA_BIEN_LA 4
    str w1, [x9, w0, uxtw]
    mov w0, #0
    ret
// Xóa [lệch, lệch + dài), từ chối độ dài âm và tràn biên arena.
.globl _vpp_native_memory_zero
_vpp_native_memory_zero:
    VAO_BO_NHO_LA
    cmp w1, #0
    b.lt Lbo_nho_la_loi
    mov w10, #0x1400
    movk w10, #0x810, lsl #16
    subs w10, w10, w1
    b.lo Lbo_nho_la_loi
    cmp w0, w10
    b.hi Lbo_nho_la_loi
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
    ret
.globl _vpp_native_memory_bind
_vpp_native_memory_bind:
    VAO_BO_NHO_LA
    KIEM_TRA_BIEN_LA 8
    tst w0, #7
    b.ne Lbo_nho_la_loi
    cmp w1, #0
    b.lt Lbo_nho_la_loi
    mov w10, #0x1400
    movk w10, #0x810, lsl #16
    cmp w1, w10
    b.hs Lbo_nho_la_loi
    add x1, x9, w1, uxtw
    str x1, [x9, w0, uxtw]  // Lưu đủ 64 bit của con trỏ
    mov w0, #0
    ret
Lbo_nho_la_loi:
    mov w0, #-1
    ret


// CP8: checked arena memmove and ASCII prefix (NEON).
// The caller supplies signed i32 arena offsets and lengths.
.globl _vpp_native_memory_copy
_vpp_native_memory_copy:
    VAO_BO_NHO_LA
    cmp w2, #0
    b.lt Lbo_nho_la_loi
    mov w10, #0x1400
    movk w10, #0x810, lsl #16
    subs w10, w10, w2
    b.lo Lbo_nho_la_loi
    cmp w0, w10
    b.hi Lbo_nho_la_loi
    cmp w1, w10
    b.hi Lbo_nho_la_loi
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
    ret

.globl _vpp_native_memory_ascii_prefix
_vpp_native_memory_ascii_prefix:
    VAO_BO_NHO_LA
    cmp w1, #0
    b.lt Lbo_nho_la_loi
    mov w10, #0x1400
    movk w10, #0x810, lsl #16
    subs w10, w10, w1
    b.lo Lbo_nho_la_loi
    cmp w0, w10
    b.hi Lbo_nho_la_loi
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
    ret

// Shared framed error exit for other primitive includes.
Lbo_nho_loi:
    mov w0, #-1
    TRA_BO_NHO

// Internal JIT context boundary. Swap the full arena, never copy heap objects
// or mix allocation metadata across the compiler and generated program.
.globl _vpp_native_jit_swap_arena
_vpp_native_jit_swap_arena:
    adrp x9, _vpp_native_arena_tls_enabled@PAGE
    ldr w9, [x9, _vpp_native_arena_tls_enabled@PAGEOFF]
    cbnz w9, Larena_swap_tls
    adrp x9, _vpp_native_arena_base@PAGE
    add x9, x9, _vpp_native_arena_base@PAGEOFF
    ldr x10, [x9]
    str x0, [x9]
    mov x0, x10
    ret

Larena_swap_tls:
    stp x19, x20, [sp, #-32]!
    str x30, [sp, #16]
    mov x19, x0
    adrp x9, _vpp_native_arena_tls_key@PAGE
    ldr x0, [x9, _vpp_native_arena_tls_key@PAGEOFF]
    bl _pthread_getspecific
    mov x20, x0
    mov x1, x19
    adrp x9, _vpp_native_arena_tls_key@PAGE
    ldr x0, [x9, _vpp_native_arena_tls_key@PAGEOFF]
    bl _pthread_setspecific
    // The existing swap ABI cannot report failure. Never continue with the
    // wrong heap if the OS cannot install the requested context.
    cbnz w0, Larena_swap_failed
    mov x0, x20
    ldr x30, [sp, #16]
    ldp x19, x20, [sp], #32
    ret
Larena_swap_failed:
    brk #0

.zerofill __DATA,__bss,_vpp_native_arena_tls_key,8,3
.zerofill __DATA,__bss,_vpp_native_arena_tls_enabled,4,2

.zerofill __DATA,__bss,_vpp_native_worker_tls_key,8,3
