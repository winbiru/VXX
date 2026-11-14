// Phép toán máy cho bộ thực thi V++ độc lập. Đối số dùng độ lệch vùng nhớ i32; con trỏ máy chỉ tồn
// tại tại ranh giới ASM.
#if defined(__APPLE__)
#define VM_SYM(x) _##x
#else
#define VM_SYM(x) x
#endif
.text
#if defined(__aarch64__) || defined(__arm64__)
.macro VM_VAO_SO_THUC
    stp x19, x20, [sp, #-48]!
    stp x21, x30, [sp, #16]
    mov w19, w0
    mov w20, w1
    mov w21, w2
    bl VM_SYM(vpp_native_memory_base)
    mov x9, x0
    cbz x9, Lvm_so_thuc_sai_bat
.endm
.macro VM_KIEM_TRA_SO_THUC offset
    tbnz \offset, #31, Lvm_so_thuc_sai_bat
    mov w10, #0x1200
    movk w10, #0x410, lsl #16
    cmp \offset, w10
    b.hi Lvm_so_thuc_sai_bat
.endm
.macro VM_TRA_SO_THUC
    ldp x21, x30, [sp, #16]
    ldp x19, x20, [sp], #48
    ret
.endm
.globl VM_SYM(vpp_native_f64_from_i32)
VM_SYM(vpp_native_f64_from_i32):
    VM_VAO_SO_THUC
    VM_KIEM_TRA_SO_THUC w19
    add x10, x9, w19, uxtw
    scvtf d0, w20
    str d0, [x10, #8]
    mov w0, #0
    VM_TRA_SO_THUC
.globl VM_SYM(vpp_native_f64_binary)
VM_SYM(vpp_native_f64_binary):
    VM_VAO_SO_THUC
    VM_KIEM_TRA_SO_THUC w19
    VM_KIEM_TRA_SO_THUC w20
    VM_KIEM_TRA_SO_THUC w21
    add x10, x9, w19, uxtw
    add x11, x9, w20, uxtw
    add x12, x9, w21, uxtw
    ldr d0, [x11, #8]
    ldr d1, [x12, #8]
    ldr w11, [x10, #4]
    cmp w11, #1
    b.eq 1f
    cmp w11, #2
    b.eq 2f
    cmp w11, #3
    b.eq 3f
    fadd d0, d0, d1
    b 4f
1:  fsub d0, d0, d1
    b 4f
2:  fmul d0, d0, d1
    b 4f
3:  fdiv d0, d0, d1
4:  str d0, [x10, #8]
    mov w0, #0
    VM_TRA_SO_THUC
.globl VM_SYM(vpp_native_f64_compare)
VM_SYM(vpp_native_f64_compare):
    VM_VAO_SO_THUC
    VM_KIEM_TRA_SO_THUC w19
    VM_KIEM_TRA_SO_THUC w20
    add x10, x9, w19, uxtw
    add x11, x9, w20, uxtw
    ldr d0, [x10, #8]
    ldr d1, [x11, #8]
    fcmp d0, d1
    mov w0, #2
    b.vs 1f
    cset w0, gt
    cset w1, mi
    sub w0, w0, w1
1:  VM_TRA_SO_THUC
.globl VM_SYM(vpp_native_f64_to_i32)
VM_SYM(vpp_native_f64_to_i32):
    VM_VAO_SO_THUC
    VM_KIEM_TRA_SO_THUC w19
    add x10, x9, w19, uxtw
    ldr d0, [x10, #8]
    fcvtzs w0, d0
    VM_TRA_SO_THUC
.globl VM_SYM(vpp_native_f64_parse)
#ifndef VPP_VM_F64_FREESTANDING
VM_SYM(vpp_native_f64_parse):
    VM_VAO_SO_THUC
    VM_KIEM_TRA_SO_THUC w19
    VM_KIEM_TRA_SO_THUC w20
    add x19, x9, w19, uxtw
    add x0, x9, w20, uxtw
    mov x1, #0
    bl VM_SYM(strtod)
    str d0, [x19, #8]
    mov w0, #0
    VM_TRA_SO_THUC
.globl VM_SYM(vpp_native_f64_text)
VM_SYM(vpp_native_f64_text):
    VM_VAO_SO_THUC
    VM_KIEM_TRA_SO_THUC w19
    VM_KIEM_TRA_SO_THUC w20
    add x10, x9, w19, uxtw
    ldr d0, [x10, #8]
    add x0, x9, w20, uxtw
    mov x1, #512
#if defined(__APPLE__)
    adrp x2, Lvm_so_thuc_dinh_dang@PAGE
    add x2, x2, Lvm_so_thuc_dinh_dang@PAGEOFF
    frintz d1, d0
    fcmp d0, d1
    b.ne 1f
    adrp x2, Lvm_so_thuc_co_dinh@PAGE
    add x2, x2, Lvm_so_thuc_co_dinh@PAGEOFF
1:
// Đối số biến thiên Darwin bắt đầu ở sp hiện tại của bên gọi.
    sub sp, sp, #16
    str d0, [sp]
    bl VM_SYM(snprintf)
    add sp, sp, #16
#else
    adrp x2, Lvm_so_thuc_dinh_dang
    add x2, x2, :lo12:Lvm_so_thuc_dinh_dang
    frintz d1, d0
    fcmp d0, d1
    b.ne 1f
    adrp x2, Lvm_so_thuc_co_dinh
    add x2, x2, :lo12:Lvm_so_thuc_co_dinh
1:
    bl VM_SYM(snprintf)
#endif
    VM_TRA_SO_THUC
Lvm_so_thuc_sai_bat:
    mov w0, #-1
    VM_TRA_SO_THUC
#else
Lvm_so_thuc_sai_bat:
    mov w0, #-1
    VM_TRA_SO_THUC
#endif
#elif defined(__x86_64__)
.macro VM_VAO_SO_THUC
    pushq %rbx
    pushq %r12
    pushq %r13
#if defined(_WIN32)
    subq $32, %rsp
    movl %ecx, %ebx
    movl %edx, %r12d
    movl %r8d, %r13d
#else
    movl %edi, %ebx
    movl %esi, %r12d
    movl %edx, %r13d
#endif
    call VM_SYM(vpp_native_memory_base)
    movq %rax, %r9
    testq %r9, %r9
    jz Lvm_so_thuc_sai_x86
.endm
.macro VM_KIEM_TRA_SO_THUC offset
    testl \offset, \offset
    js Lvm_so_thuc_sai_x86
#if defined(VPP_VM_F64_FREESTANDING)
    cmpl $(135271424-512), \offset
#else
    cmpl $68162048, \offset
#endif
    ja Lvm_so_thuc_sai_x86
.endm
.macro VM_TRA_SO_THUC
#if defined(_WIN32)
    addq $32, %rsp
#endif
    popq %r13
    popq %r12
    popq %rbx
    ret
.endm
.globl VM_SYM(vpp_native_f64_from_i32)
VM_SYM(vpp_native_f64_from_i32):
    VM_VAO_SO_THUC
    VM_KIEM_TRA_SO_THUC %ebx
    cvtsi2sdl %r12d, %xmm0
    movsd %xmm0, 8(%r9,%rbx)
    xorl %eax, %eax
    VM_TRA_SO_THUC
.globl VM_SYM(vpp_native_f64_binary)
VM_SYM(vpp_native_f64_binary):
    VM_VAO_SO_THUC
    VM_KIEM_TRA_SO_THUC %ebx
    VM_KIEM_TRA_SO_THUC %r12d
    VM_KIEM_TRA_SO_THUC %r13d
    movsd 8(%r9,%r12), %xmm0
    movsd 8(%r9,%r13), %xmm1
    movl 4(%r9,%rbx), %eax
    cmpl $1, %eax
    je 1f
    cmpl $2, %eax
    je 2f
    cmpl $3, %eax
    je 3f
    addsd %xmm1, %xmm0
    jmp 4f
1:  subsd %xmm1, %xmm0
    jmp 4f
2:  mulsd %xmm1, %xmm0
    jmp 4f
3:  divsd %xmm1, %xmm0
4:  movsd %xmm0, 8(%r9,%rbx)
    xorl %eax, %eax
    VM_TRA_SO_THUC
.globl VM_SYM(vpp_native_f64_compare)
VM_SYM(vpp_native_f64_compare):
    VM_VAO_SO_THUC
    VM_KIEM_TRA_SO_THUC %ebx
    VM_KIEM_TRA_SO_THUC %r12d
    movsd 8(%r9,%rbx), %xmm0
    ucomisd 8(%r9,%r12), %xmm0
    movl $2, %eax
    jp 1f
    seta %al
    setb %cl
    movzbl %al, %eax
    movzbl %cl, %ecx
    subl %ecx, %eax
1:  VM_TRA_SO_THUC
.globl VM_SYM(vpp_native_f64_to_i32)
VM_SYM(vpp_native_f64_to_i32):
    VM_VAO_SO_THUC
    VM_KIEM_TRA_SO_THUC %ebx
    cvttsd2sil 8(%r9,%rbx), %eax
    VM_TRA_SO_THUC
#ifndef VPP_VM_F64_FREESTANDING
.globl VM_SYM(vpp_native_f64_parse)
VM_SYM(vpp_native_f64_parse):
    VM_VAO_SO_THUC
    VM_KIEM_TRA_SO_THUC %ebx
    VM_KIEM_TRA_SO_THUC %r12d
    leaq (%r9,%rbx), %rbx
    leaq (%r9,%r12), %rdi
    xorl %esi, %esi
    call VM_SYM(strtod)
    movsd %xmm0, 8(%rbx)
    xorl %eax, %eax
    VM_TRA_SO_THUC
.globl VM_SYM(vpp_native_f64_text)
VM_SYM(vpp_native_f64_text):
    VM_VAO_SO_THUC
    VM_KIEM_TRA_SO_THUC %ebx
    VM_KIEM_TRA_SO_THUC %r12d
    movsd 8(%r9,%rbx), %xmm0
    leaq (%r9,%r12), %rdi
    movl $512, %esi
    leaq Lvm_so_thuc_dinh_dang(%rip), %rdx
    ucomisd %xmm0, %xmm0
    jp 1f
    movapd %xmm0, %xmm1
    andpd Lvm_so_thuc_tri_tuyet_doi(%rip), %xmm1
    ucomisd Lvm_so_thuc_nguyen(%rip), %xmm1
    jae 2f
    cvttsd2siq %xmm0, %rax
    cvtsi2sdq %rax, %xmm1
    ucomisd %xmm0, %xmm1
    jne 1f
2:  leaq Lvm_so_thuc_co_dinh(%rip), %rdx
1:  movl $1, %eax
    call VM_SYM(snprintf)
    VM_TRA_SO_THUC
#endif
Lvm_so_thuc_sai_x86:
    movl $-1, %eax
    VM_TRA_SO_THUC
#endif
#if defined(__APPLE__)
.section __TEXT,__cstring,cstring_literals
#elif defined(_WIN32)
.section .rdata,"dr"
#else
.section .rodata
#endif
Lvm_so_thuc_dinh_dang: .asciz "%.10g"
Lvm_so_thuc_co_dinh: .asciz "%.1f"
.p2align 4
Lvm_so_thuc_tri_tuyet_doi: .quad 0x7fffffffffffffff, 0x7fffffffffffffff
Lvm_so_thuc_nguyen: .quad 0x4330000000000000
.text
#undef VM_SYM
