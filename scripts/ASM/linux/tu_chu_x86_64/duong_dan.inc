// Lấy đường chương trình, thư mục hiện tại và thư mục nhà.

.globl vpp_native_os_executable_path
vpp_native_os_executable_path:
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
    movq %rdi, %rsi
    leaq Lchuong_trinh_lien_ket(%rip), %rdi
    movl $4095, %edx
    movq %rsi, -32(%rbp)
    call Llinux_doc_lien_ket
    cmpq $0, %rax
    jl Lbo_nho_loi
    cmpq $4095, %rax
    jae Lbo_nho_loi
    movq -32(%rbp), %rdi
    movb $0, (%rdi,%rax)
    leave
    ret
.globl vpp_native_os_home
vpp_native_os_home:
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
    leaq (%r11,%rdi), %rax
    movq %rax, -32(%rbp)
    leaq Lthu_muc_nha_khoa(%rip), %rdi
    call Llinux_moi_truong_gia_tri
    testq %rax, %rax
    je Lbo_nho_loi
    movq %rax, %rsi
    movq -32(%rbp), %rdi
    call Lchep_duong
    leave
    ret
.globl vpp_native_os_cwd
vpp_native_os_cwd:
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
    movl $4096, %esi
    call Llinux_lay_thu_muc
    cmpq $-1, %rax
    je Lbo_nho_loi
    movq -32(%rbp), %rsi
    movq %rsi, %rdi
    xorl %eax, %eax
1:
    cmpl $4096, %eax
    jae Lbo_nho_loi
    cmpb $0, (%rdi,%rax)
    je 2f
    incl %eax
    jmp 1b
2:
    leave
    ret
