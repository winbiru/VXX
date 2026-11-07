// Lấy đường chương trình, thư mục hiện tại và thư mục nhà.

.globl vpp_native_os_cwd
vpp_native_os_cwd:
    VAO_BO_NHO
    KIEM_TRA_DUONG %ecx
    leaq (%r11,%rcx), %rax
    movq %rax, -32(%rbp)
    subq $8256, %rsp
    leaq 64(%rsp), %rcx
    movl $4096, %edx
    call _wgetcwd
    testq %rax, %rax
    je Lthu_muc_hien_tai_chuyen_loi
    leaq 64(%rsp), %rcx
    movq -32(%rbp), %rdx
    call Lduong_utf16_sang_utf8
    testl %eax, %eax
    je Lthu_muc_hien_tai_chuyen_loi
    decl %eax             // ABI thư mục hiện tại trả độ dài không gồm NUL
    addq $8256, %rsp
    TRA_BO_NHO
Lthu_muc_hien_tai_chuyen_loi:
    addq $8256, %rsp
    jmp Lbo_nho_loi
.globl vpp_native_os_executable_path
vpp_native_os_executable_path:
    VAO_BO_NHO
    KIEM_TRA_DUONG %ecx
    leaq (%r11,%rcx), %rax
    movq %rax, -32(%rbp)
    subq $8256, %rsp
    leaq 64(%rsp), %rdx
    xorl %ecx, %ecx             // hModule=NULL: chương trình hiện tại
    movl $4096, %r8d
    call GetModuleFileNameW
    testl %eax, %eax
    je Lchuong_trinh_duong_chuyen_loi
    cmpl $4096, %eax
    jae Lchuong_trinh_duong_chuyen_loi
    leaq 64(%rsp), %rcx
    movq -32(%rbp), %rdx
    call Lduong_utf16_sang_utf8
    testl %eax, %eax
    je Lchuong_trinh_duong_chuyen_loi
    decl %eax             // GetModuleFileNameW trả số đơn vị mã, không gồm NUL
    addq $8256, %rsp
    TRA_BO_NHO
Lchuong_trinh_duong_chuyen_loi:
    addq $8256, %rsp
    jmp Lbo_nho_loi

