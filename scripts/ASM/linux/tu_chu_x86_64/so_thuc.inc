// Phép toán CPU cho số thực IEEE-754.

.text
.globl vpp_native_f64_from_i32
vpp_native_f64_from_i32:
    pushq %rbx
    pushq %r12
    pushq %r13
    movl %edi, %ebx
    movl %esi, %r12d
    movl %edx, %r13d
    call vpp_native_memory_base
    movq %rax, %r9
    testq %r9, %r9
    jz Lvm_so_thuc_sai_x86
    testl %ebx, %ebx
    js Lvm_so_thuc_sai_x86
    cmpl $68162048, %ebx
    ja Lvm_so_thuc_sai_x86
    cvtsi2sdl %r12d, %xmm0
    movsd %xmm0, 8(%r9,%rbx)
    xorl %eax, %eax
    popq %r13
    popq %r12
    popq %rbx
    ret
.globl vpp_native_f64_binary
vpp_native_f64_binary:
    pushq %rbx
    pushq %r12
    pushq %r13
    movl %edi, %ebx
    movl %esi, %r12d
    movl %edx, %r13d
    call vpp_native_memory_base
    movq %rax, %r9
    testq %r9, %r9
    jz Lvm_so_thuc_sai_x86
    testl %ebx, %ebx
    js Lvm_so_thuc_sai_x86
    cmpl $68162048, %ebx
    ja Lvm_so_thuc_sai_x86
    testl %r12d, %r12d
    js Lvm_so_thuc_sai_x86
    cmpl $68162048, %r12d
    ja Lvm_so_thuc_sai_x86
    testl %r13d, %r13d
    js Lvm_so_thuc_sai_x86
    cmpl $68162048, %r13d
    ja Lvm_so_thuc_sai_x86
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
1:
    subsd %xmm1, %xmm0
    jmp 4f
2:
    mulsd %xmm1, %xmm0
    jmp 4f
3:
    divsd %xmm1, %xmm0
4:
    movsd %xmm0, 8(%r9,%rbx)
    xorl %eax, %eax
    popq %r13
    popq %r12
    popq %rbx
    ret
.globl vpp_native_f64_compare
vpp_native_f64_compare:
    pushq %rbx
    pushq %r12
    pushq %r13
    movl %edi, %ebx
    movl %esi, %r12d
    movl %edx, %r13d
    call vpp_native_memory_base
    movq %rax, %r9
    testq %r9, %r9
    jz Lvm_so_thuc_sai_x86
    testl %ebx, %ebx
    js Lvm_so_thuc_sai_x86
    cmpl $68162048, %ebx
    ja Lvm_so_thuc_sai_x86
    testl %r12d, %r12d
    js Lvm_so_thuc_sai_x86
    cmpl $68162048, %r12d
    ja Lvm_so_thuc_sai_x86
    movsd 8(%r9,%rbx), %xmm0
    ucomisd 8(%r9,%r12), %xmm0
    movl $2, %eax
    jp 1f
    seta %al
    setb %cl
    movzbl %al, %eax
    movzbl %cl, %ecx
    subl %ecx, %eax
1:
    popq %r13
    popq %r12
    popq %rbx
    ret
.globl vpp_native_f64_to_i32
vpp_native_f64_to_i32:
    pushq %rbx
    pushq %r12
    pushq %r13
    movl %edi, %ebx
    movl %esi, %r12d
    movl %edx, %r13d
    call vpp_native_memory_base
    movq %rax, %r9
    testq %r9, %r9
    jz Lvm_so_thuc_sai_x86
    testl %ebx, %ebx
    js Lvm_so_thuc_sai_x86
    cmpl $68162048, %ebx
    ja Lvm_so_thuc_sai_x86
    cvttsd2sil 8(%r9,%rbx), %eax
    popq %r13
    popq %r12
    popq %rbx
    ret
Lvm_so_thuc_sai_x86:
    movl $-1, %eax
    popq %r13
    popq %r12
    popq %rbx
    ret

// Decimal conversion is implemented by the V++ runtime; adapt its stack ABI
// to the SysV register ABI used by the VM native primitives.
.globl vpp_native_f64_parse
.type vpp_native_f64_parse, @function
vpp_native_f64_parse:
    subq $40, %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_vm_f64_parse"
    addq $40, %rsp
    ret
.size vpp_native_f64_parse, .-vpp_native_f64_parse

.globl vpp_native_f64_text
.type vpp_native_f64_text, @function
vpp_native_f64_text:
    subq $40, %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_vm_f64_text"
    addq $40, %rsp
    ret
.size vpp_native_f64_text, .-vpp_native_f64_text

.section .rodata
Lvm_so_thuc_dinh_dang:
.asciz "%.10g"
Lvm_so_thuc_co_dinh:
.asciz "%.1f"
.p2align 4
Lvm_so_thuc_tri_tuyet_doi:
.quad 0x7fffffffffffffff, 0x7fffffffffffffff
Lvm_so_thuc_nguyen:
.quad 0x4330000000000000
.text
