// Pointer-safe JIT boundary. V++ owns code generation, layout and relocations.
// service(op,a,b): 0 host; 1 publish byte handle; 2 invoke(handle,packet);
// 3 release(handle); 4 runtime address(selector,8-byte output handle).
// 5 publish image: bytes followed by aligned i32 pointer relocation offsets;
// b = image length. Relocation targets are image-relative i32 offsets.
// Packet: entry offset, argc, eight i32 arguments, i32 result (44 bytes).
// 7 invoke standalone zero-arity entry; packet adds argv offset at byte 44.
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
    cmp w19, #10
    b.eq Ljit_staging_new
    cmp w19, #11
    b.lt 1f
    cmp w19, #16
    b.le Ljit_lookup
1:  cbz w19, Ljit_host
    cmp w19, #1
    b.eq Ljit_publish
    cmp w19, #5
    b.eq Ljit_publish
    cmp w19, #4
    b.eq Ljit_address
    cmp w19, #7
    b.eq Ljit_lookup
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
    adrp x9, _vpp_jit_guest_scope@PAGE
    ldr w9, [x9, _vpp_jit_guest_scope@PAGEOFF]
    ldr w10, [x22, #28]
    cmp w9, w10
    b.ne Ljit_invalid
    cmp w20, #0
    b.le Ljit_invalid
    cmp w19, #3
    b.eq Ljit_release
    ldr w9, [x22, #20]
    cmp w9, #10
    b.eq Ljit_staging
    cmp w19, #10
    b.ge Ljit_invalid
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
    cmp w19, #7
    b.eq Ljit_program
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
Ljit_program:
    // No caller handles cross the arena boundary. Reject nested invocation.
    cbnz w0, Ljit_invalid
    adrp x9, _vpp_jit_argv_offset@PAGE
    ldr w9, [x9, _vpp_jit_argv_offset@PAGEOFF]
    cbnz w9, Ljit_invalid
    mov w0, w21
    bl _vpp_native_bytes_length
    cmp w0, #48
    b.lt Ljit_invalid
    mov w0, w21
    mov w1, #44
    bl _vpp_native_bytes_load_i32
    mov w26, w0
    cmp w26, #0
    b.le Ljit_invalid
    bl _vpp_native_argc
    cmp w26, w0
    b.hs Ljit_invalid
    adrp x9, _vpp_jit_argv_offset@PAGE
    str w26, [x9, _vpp_jit_argv_offset@PAGEOFF]
    mov x0, #0
    bl _vpp_native_jit_swap_arena
    mov x25, x0
    bl _vpp_native_memory_base
    cbz x0, Ljit_program_alloc_failed
    mov x26, x0
    blr x24
    str w0, [sp, #112]
    // Release every live file handle before discarding the guest arena.
    add x26, x26, #2064
    mov w23, #64
Ljit_program_close_files:
    ldr w0, [x26], #16
    cbz w0, 1f
    bl _vpp_native_file_close
1:  subs w23, w23, #1
    b.ne Ljit_program_close_files
    mov x0, x25
    bl _vpp_native_jit_swap_arena
    mov x1, #0x1400
    movk x1, #0x810, lsl #16
    mov x16, #73
    svc #0x80
    adrp x9, _vpp_jit_argv_offset@PAGE
    str wzr, [x9, _vpp_jit_argv_offset@PAGEOFF]
    ldr w2, [sp, #112]
    mov w0, w21
    mov w1, #40
    bl _vpp_native_bytes_store_i32
    b Ljit_done
Ljit_program_alloc_failed:
    mov x0, x25
    bl _vpp_native_jit_swap_arena
    adrp x9, _vpp_jit_argv_offset@PAGE
    str wzr, [x9, _vpp_jit_argv_offset@PAGEOFF]
    b Ljit_resource
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
Ljit_staging_new:
    cmp w19, #10
    b.ne Ljit_image_size_ready
    cmp w20, #0
    b.le Ljit_invalid
    mov w9, #0x1000000
    cmp w20, w9
    b.hi Ljit_invalid
    mov w23, w20
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
    cmp w19, #10
    b.ne 1f
    ldr w0, [x25]
    add w0, w0, #1
    str w0, [x25]
    str x24, [x22]
    str w23, [x22, #8]
    str wzr, [x22, #12]
    str w0, [x22, #16]
    str w19, [x22, #20]
    str w23, [x22, #24]
    adrp x9, _vpp_jit_guest_scope@PAGE
    ldr w9, [x9, _vpp_jit_guest_scope@PAGEOFF]
    str w9, [x22, #28]
    b Ljit_done
1:  mov w26, #0
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
    str wzr, [x22, #24]
    adrp x9, _vpp_jit_guest_scope@PAGE
    ldr w9, [x9, _vpp_jit_guest_scope@PAGEOFF]
    str w9, [x22, #28]
    b Ljit_done
Ljit_rollback:
    mov x0, x24
    mov w1, w23
    mov x16, #73                 // Darwin SYS_munmap
    svc #0x80
    b Ljit_resource
// Staging mapping is RW until sealing. No native pointer is exposed.
Ljit_staging:
    ldr x24, [x22]
    ldr w23, [x22, #12]
    cmp w19, #11
    b.eq Ljit_stage_word
    cmp w19, #12
    b.eq Ljit_stage_byte
    cmp w19, #13
    b.eq Ljit_stage_read_byte
    cmp w19, #14
    b.eq Ljit_stage_read_word
    cmp w19, #15
    b.eq Ljit_stage_patch
    cmp w19, #16
    b.eq Ljit_stage_seal
    b Ljit_invalid
Ljit_stage_word:
    add w9, w23, #4
    ldr w10, [x22, #8]
    cmp w9, w10
    b.hi Ljit_invalid
    str w21, [x24, w23, uxtw]
    str w9, [x22, #12]
    b Ljit_stage_ok
Ljit_stage_byte:
    cmp w21, #255
    b.hi Ljit_invalid
    ldr w9, [x22, #8]
    cmp w23, w9
    b.hs Ljit_invalid
    strb w21, [x24, w23, uxtw]
    add w23, w23, #1
    str w23, [x22, #12]
    b Ljit_stage_ok
Ljit_stage_read_byte:
    cmp w21, w23
    b.hs Ljit_invalid
    ldrb w0, [x24, w21, uxtw]
    b Ljit_done
Ljit_stage_read_word:
    cmp w23, #4
    b.lo Ljit_invalid
    sub w9, w23, #4
    cmp w21, w9
    b.hi Ljit_invalid
    ldr w0, [x24, w21, uxtw]
    b Ljit_done
Ljit_stage_patch:
    mov w0, w21
    bl _vpp_native_bytes_length
    cmp w0, #8
    b.ne Ljit_invalid
    mov w0, w21
    mov w1, #0
    bl _vpp_native_bytes_load_i32
    cmp w23, #4
    b.lo Ljit_invalid
    sub w9, w23, #4
    cmp w0, w9
    b.hi Ljit_invalid
    mov w26, w0
    mov w0, w21
    mov w1, #4
    bl _vpp_native_bytes_load_i32
    str w0, [x24, w26, uxtw]
Ljit_stage_ok:
    mov w0, #0
    b Ljit_done
Ljit_stage_seal:
    cmp w21, #0
    b.le Ljit_invalid
    cmp w21, w23
    b.hi Ljit_invalid
    sub w9, w23, w21
    tst w9, #3
    b.ne Ljit_invalid
    // A partial relocation cannot be retried or written; release only.
    mov w9, #17
    str w9, [x22, #20]
    mov w26, w21
Ljit_stage_reloc:
    cmp w26, w23
    b.hs Ljit_stage_protect
    ldr w9, [x24, w26, uxtw]
    tst w9, #7
    b.ne Ljit_invalid
    cmp w21, #8
    b.lo Ljit_invalid
    sub w10, w21, #8
    cmp w9, w10
    b.hi Ljit_invalid
    add x9, x24, w9, uxtw
    ldr x10, [x9]
    cmp x10, x21
    b.hs Ljit_invalid
    add x10, x24, x10
    str x10, [x9]
    add w26, w26, #4
    b Ljit_stage_reloc
Ljit_stage_protect:
    mov x0, x24
    mov w1, w21
    bl Ljit_flush_cache
    mov x0, x24
    mov w1, w21
    mov w2, #5
    mov x16, #74
    svc #0x80
    cbnz w0, Ljit_resource
    str w21, [x22, #8]
    mov w9, #5
    str w9, [x22, #20]
    ldr w0, [x22, #16]
    b Ljit_done
Ljit_release:
    ldr x0, [x22]
    ldr w1, [x22, #24]
    cbnz w1, 1f
    ldr w1, [x22, #8]
1:  mov x16, #73
    svc #0x80
    cbnz w0, Ljit_resource
    str xzr, [x22]
    str wzr, [x22, #16]
    str wzr, [x22, #20]
    str wzr, [x22, #24]
    str wzr, [x22, #28]
    b Ljit_done
Ljit_address:
    mov w0, w21
    bl _vpp_native_bytes_length
    cmp w0, #8
    b.lt Ljit_invalid
    cmp w20, #1
    b.eq Ljit_abi_1
    cmp w20, #2
    b.eq Ljit_abi_2
    cmp w20, #3
    b.eq Ljit_abi_3
    cmp w20, #4
    b.eq Ljit_abi_4
    cmp w20, #5
    b.eq Ljit_abi_5
    cmp w20, #6
    b.eq Ljit_abi_6
    cmp w20, #7
    b.eq Ljit_abi_7
    cmp w20, #8
    b.eq Ljit_abi_8
    cmp w20, #9
    b.eq Ljit_abi_9
    cmp w20, #10
    b.eq Ljit_abi_10
    cmp w20, #11
    b.eq Ljit_abi_11
    cmp w20, #12
    b.eq Ljit_abi_12
    cmp w20, #13
    b.eq Ljit_abi_13
    cmp w20, #16
    b.eq Ljit_abi_16
    cmp w20, #17
    b.eq Ljit_abi_17
    cmp w20, #18
    b.eq Ljit_abi_18
    cmp w20, #23
    b.eq Ljit_abi_23
    cmp w20, #24
    b.eq Ljit_abi_24
    cmp w20, #25
    b.eq Ljit_abi_25
    cmp w20, #26
    b.eq Ljit_abi_26
    cmp w20, #27
    b.eq Ljit_abi_27
    cmp w20, #43
    b.eq Ljit_abi_43
    cmp w20, #44
    b.eq Ljit_abi_44
    cmp w20, #45
    b.eq Ljit_abi_45
    cmp w20, #46
    b.eq Ljit_abi_46
    cmp w20, #71
    b.eq Ljit_abi_71
    cmp w20, #72
    b.eq Ljit_abi_72
    cmp w20, #73
    b.eq Ljit_abi_73
    cmp w20, #76
    b.eq Ljit_abi_76
    cmp w20, #14
    b.eq Ljit_abi_14
    cmp w20, #15
    b.eq Ljit_abi_15
    cmp w20, #28
    b.eq Ljit_abi_28
    cmp w20, #29
    b.eq Ljit_abi_29
    cmp w20, #30
    b.eq Ljit_abi_30
    cmp w20, #31
    b.eq Ljit_abi_31
    cmp w20, #32
    b.eq Ljit_abi_32
    cmp w20, #38
    b.eq Ljit_abi_38
    cmp w20, #39
    b.eq Ljit_abi_39
    cmp w20, #35
    b.eq Ljit_abi_35
    cmp w20, #77
    b.eq Ljit_abi_77
    cmp w20, #33
    b.eq Ljit_abi_33
    cmp w20, #34
    b.eq Ljit_abi_34
    cmp w20, #36
    b.eq Ljit_abi_36
    cmp w20, #37
    b.eq Ljit_abi_37
    cmp w20, #47
    b.eq Ljit_abi_47
    cmp w20, #48
    b.eq Ljit_abi_48
    cmp w20, #49
    b.eq Ljit_abi_49
    cmp w20, #50
    b.eq Ljit_abi_50
    cmp w20, #53
    b.eq Ljit_abi_53
    cmp w20, #54
    b.eq Ljit_abi_54
    cmp w20, #55
    b.eq Ljit_abi_55
    cmp w20, #56
    b.eq Ljit_abi_56
    cmp w20, #57
    b.eq Ljit_abi_57
    cmp w20, #58
    b.eq Ljit_abi_58
    cmp w20, #59
    b.eq Ljit_abi_59
    cmp w20, #60
    b.eq Ljit_abi_60
    cmp w20, #65
    b.eq Ljit_abi_65
    cmp w20, #66
    b.eq Ljit_abi_66
    cmp w20, #67
    b.eq Ljit_abi_67
    cmp w20, #68
    b.eq Ljit_abi_68
    cmp w20, #69
    b.eq Ljit_abi_69
    cmp w20, #70
    b.eq Ljit_abi_70
    cmp w20, #74
    b.eq Ljit_abi_74
    cmp w20, #75
    b.eq Ljit_abi_75
    cmp w20, #78
    b.eq Ljit_abi_78
    cmp w20, #81
    b.eq Ljit_abi_81
    cmp w20, #82
    b.eq Ljit_abi_82
    cmp w20, #85
    b.eq Ljit_abi_85
    cmp w20, #86
    b.eq Ljit_abi_86
    cmp w20, #87
    b.eq Ljit_abi_87
    b Ljit_invalid
Ljit_abi_14:
    adrp x24, _vpp_native_argc@PAGE
    add x24, x24, _vpp_native_argc@PAGEOFF
    b Ljit_save_address
Ljit_abi_15:
    adrp x24, _vpp_native_argv@PAGE
    add x24, x24, _vpp_native_argv@PAGEOFF
    b Ljit_save_address
Ljit_abi_28:
    adrp x24, _vpp_native_memory_read_byte@PAGE
    add x24, x24, _vpp_native_memory_read_byte@PAGEOFF
    b Ljit_save_address
Ljit_abi_29:
    adrp x24, _vpp_native_memory_write_byte@PAGE
    add x24, x24, _vpp_native_memory_write_byte@PAGEOFF
    b Ljit_save_address
Ljit_abi_30:
    adrp x24, _vpp_native_memory_read_i32@PAGE
    add x24, x24, _vpp_native_memory_read_i32@PAGEOFF
    b Ljit_save_address
Ljit_abi_31:
    adrp x24, _vpp_native_memory_write_i32@PAGE
    add x24, x24, _vpp_native_memory_write_i32@PAGEOFF
    b Ljit_save_address
Ljit_abi_32:
    adrp x24, _vpp_native_memory_bind@PAGE
    add x24, x24, _vpp_native_memory_bind@PAGEOFF
    b Ljit_save_address
Ljit_abi_38:
    adrp x24, _vpp_native_os_argument_length@PAGE
    add x24, x24, _vpp_native_os_argument_length@PAGEOFF
    b Ljit_save_address
Ljit_abi_39:
    adrp x24, _vpp_native_os_argument_byte@PAGE
    add x24, x24, _vpp_native_os_argument_byte@PAGEOFF
    b Ljit_save_address
Ljit_abi_1:
    adrp x24, _vpp_native_print_i32@PAGE
    add x24, x24, _vpp_native_print_i32@PAGEOFF
    b Ljit_save_address
Ljit_abi_2:
    adrp x24, _vpp_native_i32_div@PAGE
    add x24, x24, _vpp_native_i32_div@PAGEOFF
    b Ljit_save_address
Ljit_abi_3:
    adrp x24, _vpp_native_bytes_new@PAGE
    add x24, x24, _vpp_native_bytes_new@PAGEOFF
    b Ljit_save_address
Ljit_abi_4:
    adrp x24, _vpp_native_bytes_length@PAGE
    add x24, x24, _vpp_native_bytes_length@PAGEOFF
    b Ljit_save_address
Ljit_abi_5:
    adrp x24, _vpp_native_bytes_get@PAGE
    add x24, x24, _vpp_native_bytes_get@PAGEOFF
    b Ljit_save_address
Ljit_abi_6:
    adrp x24, _vpp_native_bytes_append@PAGE
    add x24, x24, _vpp_native_bytes_append@PAGEOFF
    b Ljit_save_address
Ljit_abi_7:
    adrp x24, _vpp_native_bytes_push_i32@PAGE
    add x24, x24, _vpp_native_bytes_push_i32@PAGEOFF
    b Ljit_save_address
Ljit_abi_8:
    adrp x24, _vpp_native_bytes_load_i32@PAGE
    add x24, x24, _vpp_native_bytes_load_i32@PAGEOFF
    b Ljit_save_address
Ljit_abi_9:
    adrp x24, _vpp_native_bytes_store_i32@PAGE
    add x24, x24, _vpp_native_bytes_store_i32@PAGEOFF
    b Ljit_save_address
Ljit_abi_10:
    adrp x24, _vpp_native_bytes_clear@PAGE
    add x24, x24, _vpp_native_bytes_clear@PAGEOFF
    b Ljit_save_address
Ljit_abi_11:
    adrp x24, _vpp_native_bytes_concat@PAGE
    add x24, x24, _vpp_native_bytes_concat@PAGEOFF
    b Ljit_save_address
Ljit_abi_12:
    adrp x24, _vpp_native_bytes_equal@PAGE
    add x24, x24, _vpp_native_bytes_equal@PAGEOFF
    b Ljit_save_address
Ljit_abi_13:
    adrp x24, _vpp_native_bytes_free@PAGE
    add x24, x24, _vpp_native_bytes_free@PAGEOFF
    b Ljit_save_address
Ljit_abi_16:
    adrp x24, _vpp_native_file_open_read@PAGE
    add x24, x24, _vpp_native_file_open_read@PAGEOFF
    b Ljit_save_address
Ljit_abi_17:
    adrp x24, _vpp_native_file_read_block@PAGE
    add x24, x24, _vpp_native_file_read_block@PAGEOFF
    b Ljit_save_address
Ljit_abi_18:
    adrp x24, _vpp_native_file_close@PAGE
    add x24, x24, _vpp_native_file_close@PAGEOFF
    b Ljit_save_address
Ljit_abi_23:
    adrp x24, _vpp_native_bytes_slice@PAGE
    add x24, x24, _vpp_native_bytes_slice@PAGEOFF
    b Ljit_save_address
Ljit_abi_24:
    adrp x24, _vpp_native_bytes_find@PAGE
    add x24, x24, _vpp_native_bytes_find@PAGEOFF
    b Ljit_save_address
Ljit_abi_25:
    adrp x24, _vpp_native_bytes_print@PAGE
    add x24, x24, _vpp_native_bytes_print@PAGEOFF
    b Ljit_save_address
Ljit_abi_26:
    adrp x24, _vpp_native_file_open_write@PAGE
    add x24, x24, _vpp_native_file_open_write@PAGEOFF
    b Ljit_save_address
Ljit_abi_27:
    adrp x24, _vpp_native_file_write_block@PAGE
    add x24, x24, _vpp_native_file_write_block@PAGEOFF
    b Ljit_save_address
Ljit_abi_43:
    adrp x24, _vpp_native_read_i32@PAGE
    add x24, x24, _vpp_native_read_i32@PAGEOFF
    b Ljit_save_address
Ljit_abi_44:
    adrp x24, _vpp_native_read_status@PAGE
    add x24, x24, _vpp_native_read_status@PAGEOFF
    b Ljit_save_address
Ljit_abi_45:
    adrp x24, _vpp_native_file_read_byte@PAGE
    add x24, x24, _vpp_native_file_read_byte@PAGEOFF
    b Ljit_save_address
Ljit_abi_46:
    adrp x24, _vpp_native_file_write_byte@PAGE
    add x24, x24, _vpp_native_file_write_byte@PAGEOFF
    b Ljit_save_address
Ljit_abi_71:
    adrp x24, _vpp_native_os_clock@PAGE
    add x24, x24, _vpp_native_os_clock@PAGEOFF
    b Ljit_save_address
Ljit_abi_72:
    adrp x24, _vpp_native_os_sleep@PAGE
    add x24, x24, _vpp_native_os_sleep@PAGEOFF
    b Ljit_save_address
Ljit_abi_73:
    adrp x24, _vpp_native_os_entropy@PAGE
    add x24, x24, _vpp_native_os_entropy@PAGEOFF
    b Ljit_save_address
Ljit_abi_76:
    adrp x24, _vpp_native_os_process@PAGE
    add x24, x24, _vpp_native_os_process@PAGEOFF
    b Ljit_save_address
Ljit_abi_35:
    adrp x24, _vpp_native_os_write@PAGE
    add x24, x24, _vpp_native_os_write@PAGEOFF
    b Ljit_save_address
Ljit_abi_77:
    adrp x24, _vpp_native_os_socket@PAGE
    add x24, x24, _vpp_native_os_socket@PAGEOFF
    b Ljit_save_address
Ljit_abi_33:
    adrp x24, _vpp_native_os_open@PAGE
    add x24, x24, _vpp_native_os_open@PAGEOFF
    b Ljit_save_address
Ljit_abi_34:
    adrp x24, _vpp_native_os_read@PAGE
    add x24, x24, _vpp_native_os_read@PAGEOFF
    b Ljit_save_address
Ljit_abi_36:
    adrp x24, _vpp_native_os_close@PAGE
    add x24, x24, _vpp_native_os_close@PAGEOFF
    b Ljit_save_address
Ljit_abi_37:
    adrp x24, _vpp_native_os_errno@PAGE
    add x24, x24, _vpp_native_os_errno@PAGEOFF
    b Ljit_save_address
Ljit_abi_47:
    adrp x24, _vpp_native_os_home@PAGE
    add x24, x24, _vpp_native_os_home@PAGEOFF
    b Ljit_save_address
Ljit_abi_48:
    adrp x24, _vpp_native_os_cwd@PAGE
    add x24, x24, _vpp_native_os_cwd@PAGEOFF
    b Ljit_save_address
Ljit_abi_49:
    adrp x24, _vpp_native_os_mkstemp@PAGE
    add x24, x24, _vpp_native_os_mkstemp@PAGEOFF
    b Ljit_save_address
Ljit_abi_50:
    adrp x24, _vpp_native_os_rename@PAGE
    add x24, x24, _vpp_native_os_rename@PAGEOFF
    b Ljit_save_address
Ljit_abi_53:
    adrp x24, _vpp_native_os_stat@PAGE
    add x24, x24, _vpp_native_os_stat@PAGEOFF
    b Ljit_save_address
Ljit_abi_54:
    adrp x24, _vpp_native_os_lstat@PAGE
    add x24, x24, _vpp_native_os_lstat@PAGEOFF
    b Ljit_save_address
Ljit_abi_55:
    adrp x24, _vpp_native_os_access_exec@PAGE
    add x24, x24, _vpp_native_os_access_exec@PAGEOFF
    b Ljit_save_address
Ljit_abi_56:
    adrp x24, _vpp_native_os_unlink@PAGE
    add x24, x24, _vpp_native_os_unlink@PAGEOFF
    b Ljit_save_address
Ljit_abi_57:
    adrp x24, _vpp_native_os_env_copy@PAGE
    add x24, x24, _vpp_native_os_env_copy@PAGEOFF
    b Ljit_save_address
Ljit_abi_58:
    adrp x24, _vpp_native_os_executable_path@PAGE
    add x24, x24, _vpp_native_os_executable_path@PAGEOFF
    b Ljit_save_address
Ljit_abi_59:
    adrp x24, _vpp_native_os_mkdir@PAGE
    add x24, x24, _vpp_native_os_mkdir@PAGEOFF
    b Ljit_save_address
Ljit_abi_60:
    adrp x24, _vpp_native_os_fstat@PAGE
    add x24, x24, _vpp_native_os_fstat@PAGEOFF
    b Ljit_save_address
Ljit_abi_65:
    adrp x24, _vpp_native_f64_from_i32@PAGE
    add x24, x24, _vpp_native_f64_from_i32@PAGEOFF
    b Ljit_save_address
Ljit_abi_66:
    adrp x24, _vpp_native_f64_binary@PAGE
    add x24, x24, _vpp_native_f64_binary@PAGEOFF
    b Ljit_save_address
Ljit_abi_67:
    adrp x24, _vpp_native_f64_compare@PAGE
    add x24, x24, _vpp_native_f64_compare@PAGEOFF
    b Ljit_save_address
Ljit_abi_68:
    adrp x24, _vpp_native_f64_to_i32@PAGE
    add x24, x24, _vpp_native_f64_to_i32@PAGEOFF
    b Ljit_save_address
Ljit_abi_69:
    adrp x24, _vpp_native_f64_parse@PAGE
    add x24, x24, _vpp_native_f64_parse@PAGEOFF
    b Ljit_save_address
Ljit_abi_70:
    adrp x24, _vpp_native_f64_text@PAGE
    add x24, x24, _vpp_native_f64_text@PAGEOFF
    b Ljit_save_address
Ljit_abi_74:
    adrp x24, _vpp_native_os_identity@PAGE
    add x24, x24, _vpp_native_os_identity@PAGEOFF
    b Ljit_save_address
Ljit_abi_75:
    adrp x24, _vpp_native_os_directory@PAGE
    add x24, x24, _vpp_native_os_directory@PAGEOFF
    b Ljit_save_address
Ljit_abi_78:
    adrp x24, _vpp_native_os_thread_cpu_clock@PAGE
    add x24, x24, _vpp_native_os_thread_cpu_clock@PAGEOFF
    b Ljit_save_address
Ljit_abi_81:
    adrp x24, _vpp_native_os_fchmod@PAGE
    add x24, x24, _vpp_native_os_fchmod@PAGEOFF
    b Ljit_save_address
Ljit_abi_82:
    adrp x24, _vpp_native_memory_zero@PAGE
    add x24, x24, _vpp_native_memory_zero@PAGEOFF
    b Ljit_save_address
Ljit_abi_85:
    adrp x24, _vpp_native_memory_copy@PAGE
    add x24, x24, _vpp_native_memory_copy@PAGEOFF
    b Ljit_save_address
Ljit_abi_86:
    adrp x24, _vpp_native_memory_ascii_prefix@PAGE
    add x24, x24, _vpp_native_memory_ascii_prefix@PAGEOFF
    b Ljit_save_address
Ljit_abi_87:
    adrp x24, _vpp_native_os_seek@PAGE
    add x24, x24, _vpp_native_os_seek@PAGEOFF
    b Ljit_save_address
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
// Dynamic guest owns only mappings allocated during its run. Release them
// before restoring the compiler arena; parent handles are never accessible.
.globl _vpp_native_jit_guest_begin
_vpp_native_jit_guest_begin:
    adrp x9, _vpp_jit_guest_scope@PAGE
    adrp x10, _vpp_jit_next@PAGE
    ldr w10, [x10, _vpp_jit_next@PAGEOFF]
    add w10, w10, #1
    str w10, [x9, _vpp_jit_guest_scope@PAGEOFF]
    ret
.globl _vpp_native_jit_guest_end
_vpp_native_jit_guest_end:
    stp x19, x20, [sp, #-32]!
    str x30, [sp, #16]
    adrp x19, _vpp_jit_slots@PAGE
    add x19, x19, _vpp_jit_slots@PAGEOFF
    mov w20, #64
Ljit_guest_cleanup:
    adrp x9, _vpp_jit_guest_scope@PAGE
    ldr w9, [x9, _vpp_jit_guest_scope@PAGEOFF]
    ldr w10, [x19, #28]
    cmp w9, w10
    b.ne 1f
    mov w0, #3
    ldr w1, [x19, #16]
    mov w2, #0
    bl _vpp_native_jit_service
1:  add x19, x19, #32
    subs w20, w20, #1
    b.ne Ljit_guest_cleanup
    adrp x9, _vpp_jit_guest_scope@PAGE
    str wzr, [x9, _vpp_jit_guest_scope@PAGEOFF]
    ldr x30, [sp, #16]
    ldp x19, x20, [sp], #32
    ret
.zerofill __DATA,__bss,_vpp_jit_guest_scope,4,2
.zerofill __DATA,__bss,_vpp_jit_slots,2048,3
.zerofill __DATA,__bss,_vpp_jit_next,4,2
