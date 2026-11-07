// Kiểm tra chỉ số, lấy độ dài và đọc byte đối số chương trình.

// glibc cung cấp con trỏ tới ngăn xếp argc/argv ban đầu.
.globl vpp_native_argc
vpp_native_argc:
    adrp x9, :got:__libc_stack_end
    ldr x9, [x9, :got_lo12:__libc_stack_end]
    ldr x9, [x9]
    ldr w0, [x9]
    ret
Ldoi_so_con_tro:
    cmp w0, #0
    b.lt 1f
    adrp x9, :got:__libc_stack_end
    ldr x9, [x9, :got_lo12:__libc_stack_end]
    ldr x9, [x9]
    ldr w10, [x9]
    cmp w0, w10
    b.ge 1f
    add x9, x9, #8
    ldr x0, [x9, w0, uxtw #3]
    ret
1:  mov x0, #0
    ret
.globl vpp_native_os_argument_length
vpp_native_os_argument_length:
    str x30, [sp, #-16]!
    bl Ldoi_so_con_tro
    cbz x0, 2f
    mov x9, #0
    mov x11, #0x100000
1:  cmp x9, x11
    b.hi 2f
    ldrb w10, [x0, x9]
    cbz w10, 3f
    add x9, x9, #1
    b 1b
3:  mov w0, w9
    ldr x30, [sp], #16
    ret
2:  mov w0, #-1
    ldr x30, [sp], #16
    ret
.globl vpp_native_os_argument_byte
vpp_native_os_argument_byte:
    stp x1, x30, [sp, #-16]!
    bl Ldoi_so_con_tro
    cbz x0, 2f
    ldr w1, [sp]
    cmp w1, #0
    b.lt 2f
    mov w11, #0x100000
    cmp w1, w11
    b.hs 2f
    mov x9, #0
1:  ldrb w10, [x0, x9]
    cbz w10, 2f
    cmp w9, w1
    b.eq 3f
    add x9, x9, #1
    b 1b
3:  mov w0, w10
    ldp x1, x30, [sp], #16
    ret
2:  mov w0, #-1
    ldp x1, x30, [sp], #16
    ret

