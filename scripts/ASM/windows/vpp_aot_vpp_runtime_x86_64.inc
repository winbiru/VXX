// Win64 dynamic calls and metadata use the same Microsoft x64 ABI as
// the V++ i32 runtime. Native handles and addresses remain 64 bit.
.att_syntax prefix
.text
.p2align 4
.macro AOT_VPP ten, so_doi_so
    .globl vpp_native_dynamic_\ten
vpp_native_dynamic_\ten:
    // Giữ nguyên sáu thanh ghi, các đối số thứ bảy trở đi và địa chỉ trả về.
    jmp vpp_fn_aot_\ten
.endm
.macro AOT_VPP_GAP_I32 ten, so_doi_so
    AOT_VPP \ten, \so_doi_so
.endm
#include "../common/vpp_aot_vpp_exports.inc"

.globl vpp_native_aot_info
vpp_native_aot_info:
    leaq vpp_aot_spec(%rip), %r9
    cmpl $4, %ecx
    ja .Laot_thong_tin_bang_x
    cmpl $3, %ecx
    je .Laot_thong_tin_ngoai_x
    cmpl $4, %ecx
    je .Laot_thong_tin_bien_dich_x
    shll $4, %ecx
    movl (%r9,%rcx), %eax
    ret
.Laot_thong_tin_ngoai_x:
    movl 64(%r9), %eax
    ret
.Laot_thong_tin_bien_dich_x:
    movl 56(%r9), %eax
    ret
.Laot_thong_tin_bang_x:
    testl %edx, %edx
    js .Laot_thong_tin_sai_x
    cmpl $11, %ecx
    ja .Laot_thong_tin_ham_x
    cmpl $10, %ecx
    jb .Laot_thong_tin_sai_x
    cmpq (%r9), %rdx
    jae .Laot_thong_tin_sai_x
    shlq $4, %rdx
    addq 8(%r9), %rdx
    movq 8(%rdx), %rax
    cmpl $10, %ecx
    je .Laot_thong_tin_tra_ve_x
    testl %r8d, %r8d
    js .Laot_thong_tin_sai_x
    cmpq %rax, %r8
    jae .Laot_thong_tin_sai_x
    movq (%rdx), %rdx
    movzbl (%rdx,%r8), %eax
.Laot_thong_tin_tra_ve_x:
    ret
.Laot_thong_tin_ham_x:
    cmpl $30, %ecx
    jae .Laot_thong_tin_ham_i32_x
    subl $20, %ecx
    cmpl $2, %ecx
    ja .Laot_thong_tin_sai_x
    cmpq 16(%r9), %rdx
    jae .Laot_thong_tin_sai_x
    imulq $24, %rdx
    addq 24(%r9), %rdx
    movl (%rdx,%rcx,4), %eax
    ret
.Laot_thong_tin_ham_i32_x:
    subl $30, %ecx
    cmpl $1, %ecx
    ja .Laot_thong_tin_sai_x
    cmpq 80(%r9), %rdx
    jae .Laot_thong_tin_sai_x
    shlq $4, %rdx
    addq 88(%r9), %rdx
    addq $8, %rdx
    movl (%rdx,%rcx,4), %eax
    ret
.Laot_thong_tin_sai_x:
    movl $-1, %eax
    ret

.globl vpp_native_aot_invoke
vpp_native_aot_invoke:
    leaq vpp_aot_spec(%rip), %r9
    cmpl $2, %ecx
    je .Laot_goi_ham_muc_x
    testl %edx, %edx
    js .Laot_thong_tin_sai_x
    cmpl $1, %ecx
    je .Laot_goi_ham_khoi_tao_x
    testl %ecx, %ecx
    jne .Laot_thong_tin_sai_x
    cmpq 16(%r9), %rdx
    jae .Laot_thong_tin_sai_x
    imulq $24, %rdx
    addq 24(%r9), %rdx
    movq 16(%rdx), %rax
    jmp .Laot_goi_ham_goi_x
.Laot_goi_ham_khoi_tao_x:
    cmpq 32(%r9), %rdx
    jae .Laot_thong_tin_sai_x
    movq 40(%r9), %rax
    movq (%rax,%rdx,8), %rax
    jmp .Laot_goi_ham_goi_x
.Laot_goi_ham_muc_x:
    movq 48(%r9), %rax
.Laot_goi_ham_goi_x:
    movl %r8d, %ecx
    jmp *%rax

.globl vpp_native_aot_invoke_i32
vpp_native_aot_invoke_i32:
    testl %ecx, %ecx
    js .Laot_goi_i32_sai_x
    leaq vpp_aot_spec(%rip), %r9
    cmpq 80(%r9), %rcx
    jae .Laot_goi_i32_sai_x
    movq 88(%r9), %rax
    shlq $4, %rcx
    addq %rcx, %rax
    movq (%rax), %rax
    testq %rax, %rax
    je .Laot_goi_i32_sai_x
    movl %edx, %ecx
    movl %r8d, %edx
    jmp *%rax
.Laot_goi_i32_sai_x:
    xorl %eax, %eax
    ret

.globl vpp_native_aot_fold_i32
vpp_native_aot_fold_i32:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $88, %rsp
    testl %ecx, %ecx
    js .Laot_gap_i32_sai_x
    movl %edx, 32(%rsp)
    leaq vpp_aot_spec(%rip), %r9
    cmpq 80(%r9), %rcx
    jae .Laot_gap_i32_sai_x
    movq 88(%r9), %rax
    shlq $4, %rcx
    addq %rcx, %rax
    movq (%rax), %rbx
    testq %rbx, %rbx
    je .Laot_gap_i32_sai_x
    call vpp_native_memory_base
    testq %rax, %rax
    je .Laot_gap_i32_sai_x
    movl 32(%rsp), %edx
    addq %rdx, %rax
    movl (%rax), %r12d
    movl 4(%rax), %r13d
    movl 8(%rax), %r14d
    movl 12(%rax), %r15d
    movl 16(%rax), %ebp
    movl 20(%rax), %ecx
    movl %ecx, 36(%rsp)
    testl %r13d, %r13d
    jle .Laot_gap_i32_sai_x
    cmpl $4096, %r13d
    ja .Laot_gap_i32_sai_x
    testl %ebp, %ebp
    jle .Laot_gap_i32_sai_x
    testl %ecx, %ecx
    jle .Laot_gap_i32_sai_x
    movl $0, 40(%rsp)
    movl $0, 44(%rsp)
.Laot_gap_i32_vong_x:
    movl 40(%rsp), %ecx
    cmpl %r13d, %ecx
    jge .Laot_gap_i32_xong_x
    leal (%r12,%rcx), %eax
    cltd
    idivl %ebp
    leal (%r14,%rdx), %ecx
    movl %r15d, %edx
    call *%rbx
    cltd
    idivl 36(%rsp)
    addl %edx, 44(%rsp)
    addl $1, 40(%rsp)
    jmp .Laot_gap_i32_vong_x
.Laot_gap_i32_xong_x:
    movl 44(%rsp), %eax
    jmp .Laot_gap_i32_tra_x
.Laot_gap_i32_sai_x:
    xorl %eax, %eax
.Laot_gap_i32_tra_x:
    addq $88, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret

