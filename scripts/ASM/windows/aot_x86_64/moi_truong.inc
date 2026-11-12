// Tìm và chép giá trị môi trường trong giới hạn vùng nhớ.

// rcx giữ tên biến UTF-16; rdx là đích UTF-8 4096 byte trong vùng nhớ. Môi trường Windows luôn là
// Unicode; phân biệt giá trị rỗng với biến không tồn tại.
Lmoi_truong_chep_utf16:
    pushq %rbp
    movq %rsp, %rbp
    subq $8320, %rsp       // Vùng đối số dự phòng, 4096 WCHAR và đối số đã lưu
    movq %rcx, -8(%rbp)
    movq %rdx, -16(%rbp)
    xorl %ecx, %ecx
    call SetLastError
    movq -8(%rbp), %rcx
    leaq 64(%rsp), %rdx
    movl $4096, %r8d
    call GetEnvironmentVariableW
    cmpl $4096, %eax
    jae Lmoi_truong_chep_loi
    testl %eax, %eax
    jne Lmoi_truong_chep_chuyen
    call GetLastError
    testl %eax, %eax
    jne Lmoi_truong_chep_loi
    movw $0, 64(%rsp)
Lmoi_truong_chep_chuyen:
    leaq 64(%rsp), %rcx
    movq -16(%rbp), %rdx
    call Lduong_utf16_sang_utf8
    testl %eax, %eax
    je Lmoi_truong_chep_loi
    decl %eax
    leave
    ret
Lmoi_truong_chep_loi:
    movl $-1, %eax
    leave
    ret
.globl vpp_native_os_env_copy
vpp_native_os_env_copy:
    VAO_BO_NHO
    KIEM_TRA_DUONG %ecx
    KIEM_TRA_DUONG %edx
    movl %edx, -24(%rbp)
    leaq (%r11,%rcx), %rcx
    subq $8256, %rsp
    leaq 64(%rsp), %rdx
    call Lduong_utf8_sang_utf16
    testl %eax, %eax
    je Lmoi_truong_khoa_loi
    call vpp_native_memory_base
    movl -24(%rbp), %edx
    leaq (%rax,%rdx), %rdx
    leaq 64(%rsp), %rcx
    call Lmoi_truong_chep_utf16
    addq $8256, %rsp
    TRA_BO_NHO
Lmoi_truong_khoa_loi:
    addq $8256, %rsp
    jmp Lbo_nho_loi
.globl vpp_native_os_home
vpp_native_os_home:
    VAO_BO_NHO
    KIEM_TRA_DUONG %ecx
    leaq (%r11,%rcx), %rdx
    leaq Lthu_muc_nha_khoa_utf16(%rip), %rcx
    call Lmoi_truong_chep_utf16
    TRA_BO_NHO
