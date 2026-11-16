// AOT động và bộ thực thi i32 Linux cùng dùng ABI SysV AMD64.
// Chuyển cú pháp Intel sang AT&T trước khi xuất hàm và điều phối siêu dữ liệu.
.att_syntax prefix
.text
.p2align 4
// Every metadata consumer must select the same active program.
.macro AOT_ACTIVE_SPEC
    movq vpp_jit_active_spec(%rip), %r9
    testq %r9, %r9
    jne 9f
    leaq vpp_aot_spec(%rip), %r9
9:
.endm
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
// Appended source-site helper; old bridge indices are unchanged.
AOT_VPP source_site, 2

.Lworker_aot_register:
    subq $8, %rsp
    movl $11, %edi
    xorl %esi, %esi
    xorl %edx, %edx
    call vpp_native_worker_service
    testl %eax, %eax
    jnz 1f
    leaq .Lworker_aot_entry(%rip), %rdi
    call vpp_native_worker_set_entry
1:  addq $8, %rsp
    ret
.Lworker_aot_entry:
    pushq %rbx
    movq %rdi, %rbx
    movl %esi, %edi
    call vpp_fn_aot_worker_entry
    movl %eax, %esi
    movq %rbx, %rdi
    popq %rbx
    jmp vpp_native_worker_compact_result

.globl vpp_native_aot_info
vpp_native_aot_info:
    cmpl $108, %edi
    je .Ljit_x86_address
    cmpl $109, %edi
    je .Ljit_x86_run
    cmpl $110, %edi
    je .Ljit_x86_active
    cmpl $111, %edi
    je .Lworker_aot_register
    AOT_ACTIVE_SPEC
    cmpl $4, %edi
    ja .Laot_thong_tin_bang_x
    cmpl $3, %edi
    je .Laot_thong_tin_ngoai_x
    cmpl $4, %edi
    je .Laot_thong_tin_bien_dich_x
    shll $4, %edi
    movl (%r9,%rdi), %eax
    ret
.Laot_thong_tin_ngoai_x:
    movl 64(%r9), %eax
    ret
.Laot_thong_tin_bien_dich_x:
    movl 56(%r9), %eax
    ret
.Laot_thong_tin_bang_x:
    testl %esi, %esi
    js .Laot_thong_tin_sai_x
    cmpl $40, %edi
    jae .Laot_thong_tin_ffi_x
    cmpl $11, %edi
    ja .Laot_thong_tin_ham_x
    cmpl $10, %edi
    jb .Laot_thong_tin_sai_x
    cmpq (%r9), %rsi
    jae .Laot_thong_tin_sai_x
    shlq $4, %rsi
    addq 8(%r9), %rsi
    movq 8(%rsi), %rax
    cmpl $10, %edi
    je .Laot_thong_tin_tra_ve_x
    testl %edx, %edx
    js .Laot_thong_tin_sai_x
    cmpq %rax, %rdx
    jae .Laot_thong_tin_sai_x
    movq (%rsi), %rsi
    movzbl (%rsi,%rdx), %eax
.Laot_thong_tin_tra_ve_x:
    ret
.Laot_thong_tin_ham_x:
    cmpl $30, %edi
    jae .Laot_thong_tin_ham_i32_x
    subl $20, %edi
    cmpl $3, %edi
    ja .Laot_thong_tin_sai_x
    cmpq 16(%r9), %rsi
    jae .Laot_thong_tin_sai_x
    imulq $24, %rsi
    addq 24(%r9), %rsi
    movl (%rsi,%rdi,4), %eax
    ret
.Laot_thong_tin_ham_i32_x:
    subl $30, %edi
    cmpl $1, %edi
    ja .Laot_thong_tin_sai_x
    cmpq 80(%r9), %rsi
    jae .Laot_thong_tin_sai_x
    shlq $4, %rsi
    addq 88(%r9), %rsi
    addq $8, %rsi
    movl (%rsi,%rdi,4), %eax
    ret
.Laot_thong_tin_ffi_x:
    cmpl $47, %edi
    ja .Laot_thong_tin_sai_x
    cmpq 64(%r9), %rsi
    jae .Laot_thong_tin_sai_x
    imulq $72, %rsi, %r10
    addq 72(%r9), %r10
    cmpl $44, %edi
    jae .Laot_thong_tin_ffi_signature_x
    subl $40, %edi
    movq 8(%r10,%rdi,8), %r10
    testq %r10, %r10
    je .Laot_thong_tin_sai_x
    cmpl $127, %edx
    ja .Laot_thong_tin_sai_x
    xorl %r11d, %r11d
.Laot_thong_tin_ffi_byte_x:
    movzbl (%r10,%r11), %eax
    cmpl %edx, %r11d
    je .Laot_thong_tin_ffi_return_x
    testl %eax, %eax
    je .Laot_thong_tin_sai_x
    incl %r11d
    jmp .Laot_thong_tin_ffi_byte_x
.Laot_thong_tin_ffi_signature_x:
    cmpl $44, %edi
    je .Laot_thong_tin_ffi_result_x
    cmpl $45, %edi
    je .Laot_thong_tin_ffi_params_x
    cmpl $46, %edi
    je .Laot_thong_tin_ffi_extents_x
    movl (%r10), %eax
    ret
.Laot_thong_tin_ffi_result_x:
    movl 4(%r10), %eax
    ret
.Laot_thong_tin_ffi_params_x:
    cmpq $0, 40(%r10)
    jmp .Laot_thong_tin_ffi_count_x
.Laot_thong_tin_ffi_extents_x:
    cmpq $0, 56(%r10)
.Laot_thong_tin_ffi_count_x:
    setne %al
    movzbl %al, %eax
.Laot_thong_tin_ffi_return_x:
    ret
.Laot_thong_tin_sai_x:
    movl $-1, %eax
    ret

.globl vpp_native_aot_invoke
vpp_native_aot_invoke:
    AOT_ACTIVE_SPEC
    cmpl $2, %edi
    je .Laot_goi_ham_muc_x
    testl %esi, %esi
    js .Laot_thong_tin_sai_x
    cmpl $1, %edi
    je .Laot_goi_ham_khoi_tao_x
    testl %edi, %edi
    jne .Laot_thong_tin_sai_x
    cmpq 16(%r9), %rsi
    jae .Laot_thong_tin_sai_x
    imulq $24, %rsi
    addq 24(%r9), %rsi
    movq 16(%rsi), %rax
    jmp .Laot_goi_ham_goi_x
.Laot_goi_ham_khoi_tao_x:
    cmpq 32(%r9), %rsi
    jae .Laot_thong_tin_sai_x
    movq 40(%r9), %rax
    movq (%rax,%rsi,8), %rax
    jmp .Laot_goi_ham_goi_x
.Laot_goi_ham_muc_x:
    movq 48(%r9), %rax
.Laot_goi_ham_goi_x:
    movl %edx, %edi
    jmp *%rax

.globl vpp_native_aot_invoke_i32
vpp_native_aot_invoke_i32:
    testl %edi, %edi
    js .Laot_goi_i32_sai_x
    AOT_ACTIVE_SPEC
    cmpq 80(%r9), %rdi
    jae .Laot_goi_i32_sai_x
    movq 88(%r9), %rax
    shlq $4, %rdi
    addq %rdi, %rax
    movq (%rax), %rax
    testq %rax, %rax
    je .Laot_goi_i32_sai_x
    movl %esi, %edi
    movl %edx, %esi
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
    subq $40, %rsp
    testl %edi, %edi
    js .Laot_gap_i32_sai_x
    movl %esi, (%rsp)
    AOT_ACTIVE_SPEC
    cmpq 80(%r9), %rdi
    jae .Laot_gap_i32_sai_x
    movq 88(%r9), %rax
    shlq $4, %rdi
    addq %rdi, %rax
    movq (%rax), %rbx
    testq %rbx, %rbx
    je .Laot_gap_i32_sai_x
    call vpp_native_memory_base
    testq %rax, %rax
    je .Laot_gap_i32_sai_x
    movl (%rsp), %edx
    addq %rdx, %rax
    movl (%rax), %r12d
    movl 4(%rax), %r13d
    movl 8(%rax), %r14d
    movl 12(%rax), %r15d
    movl 16(%rax), %ebp
    movl 20(%rax), %ecx
    movl %ecx, 4(%rsp)
    testl %r13d, %r13d
    jle .Laot_gap_i32_sai_x
    cmpl $4096, %r13d
    ja .Laot_gap_i32_sai_x
    testl %ebp, %ebp
    jle .Laot_gap_i32_sai_x
    testl %ecx, %ecx
    jle .Laot_gap_i32_sai_x
    movl $0, 8(%rsp)
    movl $0, 12(%rsp)
.Laot_gap_i32_vong_x:
    movl 8(%rsp), %ecx
    cmpl %r13d, %ecx
    jge .Laot_gap_i32_xong_x
    leal (%r12,%rcx), %eax
    cltd
    idivl %ebp
    leal (%r14,%rdx), %edi
    movl %r15d, %esi
    call *%rbx
    cltd
    idivl 4(%rsp)
    addl %edx, 12(%rsp)
    addl $1, 8(%rsp)
    jmp .Laot_gap_i32_vong_x
.Laot_gap_i32_xong_x:
    movl 12(%rsp), %eax
    jmp .Laot_gap_i32_tra_x
.Laot_gap_i32_sai_x:
    xorl %eax, %eax
.Laot_gap_i32_tra_x:
    addq $40, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
#include "../common/vpp_jit_dynamic_x86_64.inc"
.section .note.GNU-stack,"",@progbits
