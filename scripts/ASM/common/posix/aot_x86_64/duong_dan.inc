// Lấy đường chương trình, thư mục hiện tại và thư mục nhà.

.globl PUBLIC(vpp_native_os_executable_path)
PUBLIC(vpp_native_os_executable_path):
    VAO_BO_NHO
    KIEM_TRA_DUONG %edi
    leaq (%r11,%rdi), %rdi
#ifdef __APPLE__
    leaq -40(%rbp), %rsi
    movl $4096, -40(%rbp)
    call LIB(_NSGetExecutablePath)
    testl %eax, %eax
    jne Lbo_nho_loi
    movq PUBLIC(vpp_native_arena_base)(%rip), %r11
    movl -4(%rbp), %edi
    leaq (%r11,%rdi), %rdi
    movq %rdi, %rsi
// Quét tại chỗ, không sao chép.
    xorl %eax, %eax
1:  cmpl $4095, %eax
    jae Lbo_nho_loi
    cmpb $0, (%rsi,%rax)
    je 2f
    incl %eax
    jmp 1b
2:  TRA_BO_NHO
#else
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
    TRA_BO_NHO
#endif
.globl PUBLIC(vpp_native_os_home)
PUBLIC(vpp_native_os_home):
    VAO_BO_NHO
    KIEM_TRA_DUONG %edi
    leaq (%r11,%rdi), %rax
    movq %rax, -32(%rbp)
    leaq Lthu_muc_nha_khoa(%rip), %rdi
#ifdef __APPLE__
    call LIB(getenv)
#else
    call Llinux_moi_truong_gia_tri
#endif
    testq %rax, %rax
    je Lbo_nho_loi
    movq %rax, %rsi
    movq -32(%rbp), %rdi
    call Lchep_duong
    TRA_BO_NHO
.globl PUBLIC(vpp_native_os_cwd)
PUBLIC(vpp_native_os_cwd):
    VAO_BO_NHO
    KIEM_TRA_DUONG %edi
    leaq (%r11,%rdi), %rdi
    movq %rdi, -32(%rbp)
    movl $4096, %esi
#ifdef __APPLE__
    call LIB(getcwd)
    testq %rax, %rax
#else
    call Llinux_lay_thu_muc
    cmpq $-1, %rax
#endif
    je Lbo_nho_loi
    movq -32(%rbp), %rsi
    movq %rsi, %rdi
    xorl %eax, %eax
1:  cmpl $4096, %eax
    jae Lbo_nho_loi
    cmpb $0, (%rdi,%rax)
    je 2f
    incl %eax
    jmp 1b
2:  TRA_BO_NHO

