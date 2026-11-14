// Pointer-safe JIT boundary. V++ owns code generation, layout and relocations.
// service(op,a,b): 0 host; 1 publish byte handle; 2 invoke(handle,packet);
// 3 release(handle); 4 runtime address(selector,8-byte output handle).
// 5 publish image: bytes followed by aligned i32 pointer relocation offsets;
// b = image length. Relocation targets are image-relative i32 offsets.
// Packet: entry offset, argc, eight i32 arguments, i32 result (44 bytes).
// 6 invoke two-i32 leaf at offset zero using arena scratch (left,right,result).
// Published mappings are immutable RX. Handles are never reused in a process.
.text
.p2align 2
.globl _vpp_native_jit_service
_vpp_native_jit_service:
    stp x29, x30, [sp, #-128]!
    mov x29, sp
    stp x19, x20, [sp, #16]
    stp x21, x22, [sp, #32]
    stp x23, x24, [sp, #48]
    stp x25, x26, [sp, #64]
    mov w19, w0
    mov w20, w1
    mov w21, w2
    cbz w19, Ljit_host
    cmp w19, #1
    b.eq Ljit_publish
    cmp w19, #5
    b.eq Ljit_publish
    cmp w19, #4
    b.eq Ljit_address
    cmp w19, #6
    b.eq Ljit_lookup
    cmp w19, #2
    b.lt Ljit_invalid
    cmp w19, #3
    b.gt Ljit_invalid
Ljit_lookup:
    adrp x22, _vpp_jit_slots@PAGE
    add x22, x22, _vpp_jit_slots@PAGEOFF
    mov w23, #64
Ljit_find:
    ldr w9, [x22, #16]
    cmp w9, w20
    b.eq Ljit_found
    add x22, x22, #32
    subs w23, w23, #1
    b.ne Ljit_find
    b Ljit_invalid
Ljit_found:
    cmp w20, #0
    b.le Ljit_invalid
    cmp w19, #3
    b.eq Ljit_release
    ldr w9, [x22, #20]
    cmp w9, #1
    b.ne Ljit_invalid
    cmp w19, #6
    b.eq Ljit_leaf
    mov w0, w21
    bl _vpp_native_bytes_length
    cmp w0, #44
    b.lt Ljit_invalid
    mov w0, w21
    mov w1, #0
    bl _vpp_native_bytes_load_i32
    tbnz w0, #31, Ljit_invalid
    tst w0, #3
    b.ne Ljit_invalid
    ldr w9, [x22, #8]
    cmp w0, w9
    b.hs Ljit_invalid
    ldr x24, [x22]
    add x24, x24, w0, uxtw
    mov w0, w21
    mov w1, #4
    bl _vpp_native_bytes_load_i32
    cmp w0, #8
    b.hi Ljit_invalid
    mov w23, #0
Ljit_arguments:
    mov w0, w21
    add w1, w23, #2
    lsl w1, w1, #2
    bl _vpp_native_bytes_load_i32
    add x9, sp, #80
    str w0, [x9, x23, lsl #2]
    add w23, w23, #1
    cmp w23, #8
    b.ne Ljit_arguments
    ldp w0, w1, [sp, #80]
    ldp w2, w3, [sp, #88]
    ldp w4, w5, [sp, #96]
    ldp w6, w7, [sp, #104]
    blr x24
    mov w2, w0
    mov w0, w21
    mov w1, #40
    bl _vpp_native_bytes_store_i32
    b Ljit_done
Ljit_leaf:
    // Bounds-check all three words before touching the current arena.
    tst w21, #3
    b.ne Ljit_invalid
    mov w9, #0x13f4
    movk w9, #0x810, lsl #16
    cmp w21, w9
    b.hi Ljit_invalid
    adrp x25, _vpp_native_arena_base@PAGE
    ldr x25, [x25, _vpp_native_arena_base@PAGEOFF]
    cbz x25, Ljit_invalid
    add x25, x25, w21, uxtw
    ldr x24, [x22]
    ldp w0, w1, [x25]
    blr x24
    str w0, [x25, #8]
    mov w0, #0
    b Ljit_done
Ljit_host:
    mov w0, #1
    b Ljit_done
Ljit_publish:
    mov w0, w20
    bl _vpp_native_bytes_length
    cmp w0, #0
    b.le Ljit_invalid
    cmp w19, #5
    b.eq Ljit_publish_dynamic_limit
    cmp w0, #0x100000
    b.gt Ljit_invalid
    b Ljit_publish_limit_done
Ljit_publish_dynamic_limit:
    cmp w0, #0x200000
    b.gt Ljit_invalid
Ljit_publish_limit_done:
    mov w23, w0
    str w0, [sp, #112]
    cmp w19, #5
    b.ne Ljit_image_size_ready
    cmp w21, #0
    b.le Ljit_invalid
    cmp w21, w23
    b.hi Ljit_invalid
    sub w9, w23, w21
    tst w9, #3
    b.ne Ljit_invalid
    mov w23, w21
Ljit_image_size_ready:
    adrp x22, _vpp_jit_slots@PAGE
    add x22, x22, _vpp_jit_slots@PAGEOFF
    mov w9, #64
Ljit_empty:
    ldr x10, [x22]
    cbz x10, Ljit_allocate
    add x22, x22, #32
    subs w9, w9, #1
    b.ne Ljit_empty
    b Ljit_resource
Ljit_allocate:
    adrp x25, _vpp_jit_next@PAGE
    add x25, x25, _vpp_jit_next@PAGEOFF
    ldr w9, [x25]
    cmn w9, #1
    b.mi Ljit_resource
    mov x0, #0
    mov w1, w23
    mov w2, #3
    mov w3, #0x1002
    mov x4, #-1
    mov x5, #0
    bl _mmap
    cmn x0, #1
    b.eq Ljit_resource
    mov x24, x0
    mov w26, #0
Ljit_copy:
    mov w0, w20
    mov w1, w26
    bl _vpp_native_bytes_get
    cmp w0, #255
    b.hi Ljit_rollback
    strb w0, [x24, x26]
    add w26, w26, #1
    cmp w26, w23
    b.ne Ljit_copy
    cmp w19, #5
    b.ne Ljit_image_relocated
    mov w26, w23
Ljit_pointer_relocations:
    ldr w9, [sp, #112]
    cmp w26, w9
    b.hs Ljit_image_relocated
    mov w0, w20
    mov w1, w26
    bl _vpp_native_bytes_load_i32
    tbnz w0, #31, Ljit_rollback
    tst w0, #7
    b.ne Ljit_rollback
    sub w9, w23, #8
    cmp w23, #8
    b.lo Ljit_rollback
    cmp w0, w9
    b.hi Ljit_rollback
    add x9, x24, w0, uxtw
    ldr x10, [x9]
    cmp x10, x23
    b.hs Ljit_rollback
    add x10, x24, x10
    str x10, [x9]
    add w26, w26, #4
    b Ljit_pointer_relocations
Ljit_image_relocated:
    mov x0, x24
    mov w1, w23
    bl Ljit_flush_cache
    mov x0, x24
    mov w1, w23
    mov w2, #5
    mov x16, #74                 // Darwin SYS_mprotect
    svc #0x80
    cbnz w0, Ljit_rollback
    ldr w0, [x25]
    add w0, w0, #1
    str w0, [x25]
    str x24, [x22]
    str w23, [x22, #8]
    str w0, [x22, #16]
    str w19, [x22, #20]
    b Ljit_done
Ljit_rollback:
    mov x0, x24
    mov w1, w23
    mov x16, #73                 // Darwin SYS_munmap
    svc #0x80
    b Ljit_resource
Ljit_release:
    ldr x0, [x22]
    ldr w1, [x22, #8]
    mov x16, #73
    svc #0x80
    cbnz w0, Ljit_resource
    str xzr, [x22]
    str wzr, [x22, #16]
    b Ljit_done
Ljit_address:
    mov w0, w21
    bl _vpp_native_bytes_length
    cmp w0, #8
    b.lt Ljit_invalid
    cmp w20, #1
    b.eq Ljit_print_address
    cmp w20, #2
    b.ne Ljit_invalid
    adrp x24, _vpp_native_i32_div@PAGE
    add x24, x24, _vpp_native_i32_div@PAGEOFF
    b Ljit_save_address
Ljit_print_address:
    adrp x24, _vpp_native_print_i32@PAGE
    add x24, x24, _vpp_native_print_i32@PAGEOFF
Ljit_save_address:
    mov w0, w21
    mov w1, #0
    mov w2, w24
    bl _vpp_native_bytes_store_i32
    cbnz w0, Ljit_invalid
    mov w0, w21
    mov w1, #4
    lsr x2, x24, #32
    bl _vpp_native_bytes_store_i32
    b Ljit_done
Ljit_invalid:
    mov w0, #-1
    b Ljit_done
Ljit_resource:
    mov w0, #-2
Ljit_done:
    ldp x25, x26, [sp, #64]
    ldp x23, x24, [sp, #48]
    ldp x21, x22, [sp, #32]
    ldp x19, x20, [sp, #16]
    ldp x29, x30, [sp], #128
    ret
// Walk at instruction granularity: covers every cache line without reading
// CTR_EL0, which is not available to ordinary processes on Apple silicon.
Ljit_flush_cache:
    and x12, x0, #-4
    add x13, x0, x1
Ljit_clean_data:
    dc cvau, x12
    add x12, x12, #4
    cmp x12, x13
    b.lo Ljit_clean_data
    dsb ish
    and x12, x0, #-4
Ljit_invalidate_instruction:
    ic ivau, x12
    add x12, x12, #4
    cmp x12, x13
    b.lo Ljit_invalidate_instruction
    dsb ish
    isb
    ret
// Internal dynamic bridge lookup: only relocated dynamic images, never i32
// mappings. x0 = image base, w1 = size; zero base on invalid handle.
.globl _vpp_native_jit_image
_vpp_native_jit_image:
    cmp w0, #0
    b.le Ljit_image_missing
    adrp x9, _vpp_jit_slots@PAGE
    add x9, x9, _vpp_jit_slots@PAGEOFF
    mov w10, #64
Ljit_image_lookup:
    ldr w11, [x9, #16]
    cmp w11, w0
    b.eq Ljit_image_found
    add x9, x9, #32
    subs w10, w10, #1
    b.ne Ljit_image_lookup
Ljit_image_missing:
    mov x0, #0
    mov w1, #0
    ret
Ljit_image_found:
    ldr w11, [x9, #20]
    cmp w11, #5
    b.ne Ljit_image_missing
    ldr x0, [x9]
    ldr w1, [x9, #8]
    ret
.zerofill __DATA,__bss,_vpp_jit_slots,2048,3
.zerofill __DATA,__bss,_vpp_jit_next,4,2
