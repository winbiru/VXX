// Đọc/ghi, mở/đóng và thao tác tệp.

.globl vpp_native_os_read
vpp_native_os_read:
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    cmpl $(135271424 - 0), %esi
    ja Lbo_nho_loi
    cmpl $(0), %edx
    jl Lbo_nho_loi
    movl %esi, %eax
    addq %rdx, %rax
    cmpq $(135271424), %rax
    ja Lbo_nho_loi
    leaq (%r11,%rsi), %rsi
    movslq %edx, %rdx
    call Llinux_doc
    leave
    ret
.globl vpp_native_os_write
vpp_native_os_write:
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    cmpl $(135271424 - 0), %esi
    ja Lbo_nho_loi
    cmpl $(0), %edx
    jl Lbo_nho_loi
    movl %esi, %eax
    addq %rdx, %rax
    cmpq $(135271424), %rax
    ja Lbo_nho_loi
    leaq (%r11,%rsi), %rsi
    movslq %edx, %rdx
    call Llinux_ghi
    leave
    ret
.globl vpp_native_os_open
vpp_native_os_open:
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    cmpl $(135271424 - 4096), %edi
    ja Lbo_nho_loi
    leaq (%r11,%rdi), %rdi
    cmpl $1537, %esi
    jne 1f
    movl $577, %esi
    jmp 2f
1:
    // Darwin O_WRONLY|O_CREAT|O_APPEND
    cmpl $521, %esi
    jne 2f
    // Linux O_WRONLY|O_CREAT|O_APPEND
    movl $1089, %esi
2:
    movl $438, %edx
    call Llinux_mo
    leave
    ret
.globl vpp_native_os_close
vpp_native_os_close:
    jmp Llinux_dong
// Signed i32 position, checked against the full 64-bit lseek result.
.globl vpp_native_os_seek
vpp_native_os_seek:
    movslq %esi, %rsi
    movl $8, %eax
    syscall
    cmpq $-4095, %rax
    jae Llinux_goi_he_thong_ket_qua
    cmpq $2147483647, %rax
    jle 1f
    movl $75, vpp_linux_host_errno(%rip)
    movl $-1, %eax
1:
    ret
.globl vpp_native_os_errno
vpp_native_os_errno:
    movl vpp_linux_host_errno(%rip), %eax
    ret
.globl vpp_native_os_mkstemp
vpp_native_os_mkstemp:
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    cmpl $(135271424 - 4096), %edi
    ja Lbo_nho_loi
    leaq (%r11,%rdi), %rdi
    movq %rdi, -32(%rbp)
    xorl %ecx, %ecx
.Lmay_tam_do_dai:
    cmpl $4096, %ecx
    jae Lbo_nho_loi
    cmpb $0, (%rdi,%rcx)
    je .Lmay_tam_cuoi
    incl %ecx
    jmp .Lmay_tam_do_dai
.Lmay_tam_cuoi:
    cmpl $6, %ecx
    jb Lbo_nho_loi
    leaq -6(%rdi,%rcx), %r8
    movq %r8, -24(%rbp)
    movl $6, %ecx
.Lmay_tam_kiem_tra:
    cmpb $'X', (%r8)
    jne Lbo_nho_loi
    incq %r8
    decl %ecx
    jne .Lmay_tam_kiem_tra
    movl $64, -16(%rbp)
.Lmay_tam_thu_lai:
    leaq -40(%rbp), %rdi
    movl $8, %esi
    xorl %edx, %edx
    call vpp_sys_getrandom
    cmpq $-4, %rax
    je .Lmay_tam_thu_lai
    cmpq $8, %rax
    je .Lmay_tam_ngau_nhien
    cmpq $-4095, %rax
    jae .Lmay_tam_ket_qua
    movq $-5, %rax
    jmp .Lmay_tam_ket_qua
.Lmay_tam_ngau_nhien:
    movq -40(%rbp), %rax
    movq -24(%rbp), %r8
    leaq .Lmay_tam_bang_chu(%rip), %r9
    movl $62, %r10d
    movl $6, %ecx
.Lmay_tam_lap_day:
    xorl %edx, %edx
    divq %r10
    movb (%r9,%rdx), %dl
    movb %dl, (%r8)
    incq %r8
    decl %ecx
    jne .Lmay_tam_lap_day
    movq $-100, %rdi
    movq -32(%rbp), %rsi
    movl $194, %edx
    movl $384, %ecx
    call vpp_sys_openat
    cmpq $-17, %rax
    jne .Lmay_tam_ket_qua
    decl -16(%rbp)
    jne .Lmay_tam_thu_lai
.Lmay_tam_ket_qua:
    call Llinux_goi_he_thong_ket_qua
    leave
    ret
.globl vpp_native_os_rename
vpp_native_os_rename:
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    cmpl $(135271424 - 4096), %edi
    ja Lbo_nho_loi
    cmpl $(135271424 - 4096), %esi
    ja Lbo_nho_loi
    leaq (%r11,%rdi), %rdi
    leaq (%r11,%rsi), %rsi
    call Llinux_doi_ten
    leave
    ret
