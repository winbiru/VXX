// Khởi tạo, cập nhật và kết thúc phép băm SHA-256.

// SHA-256 từng khối qua CNG. Vùng siêu dữ liệu [3904,4008) chứa hai mã tài nguyên 64 bit tại
// 3904/3912. Windows 7+ cho BCryptCreateHash tự cấp phát đối tượng băm. Kết thúc thành công hoặc
// khởi tạo thất bại đều giải phóng tài nguyên.
.globl vpp_native_sha_begin
vpp_native_sha_begin:
    VAO_BO_NHO
    cmpq $0, 3904(%r11)
    jne Lbo_nho_loi
    cmpq $0, 3912(%r11)
    jne Lbo_nho_loi
    movq %r11, -32(%rbp)
    leaq 3912(%r11), %rcx     // BCRYPT_ALG_HANDLE *
    leaq Lsha256_thuat_toan(%rip), %rdx
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    call BCryptOpenAlgorithmProvider
    testl %eax, %eax
    js Lbo_nho_loi
    movq -32(%rbp), %r10
    movq 3912(%r10), %rcx
    leaq 3904(%r10), %rdx     // BCRYPT_HASH_HANDLE *
    xorl %r8d, %r8d           // Bộ cung cấp tự cấp phát đối tượng băm
    xorl %r9d, %r9d
    movq $0, 32(%rsp)         // Không có khóa bí mật
    movq $0, 40(%rsp)         // Độ dài khóa bí mật bằng 0
    movq $0, 48(%rsp)         // Cờ
    call BCryptCreateHash
    testl %eax, %eax
    js Lsha_bat_dau_da_loi
    xorl %eax, %eax
    TRA_BO_NHO
Lsha_bat_dau_da_loi:
    movq -32(%rbp), %r10
    movq 3912(%r10), %rcx
    xorl %edx, %edx
    call BCryptCloseAlgorithmProvider
    movq -32(%rbp), %r10
    movq $0, 3904(%r10)
    movq $0, 3912(%r10)
    jmp Lbo_nho_loi

.globl vpp_native_sha_update
vpp_native_sha_update:
    VAO_BO_NHO
    KIEM_TRA_LECH %ecx, 0
    cmpl $0, %edx
    jl Lbo_nho_loi
    movl %edx, -24(%rbp)
    movl %ecx, %eax
    movl %edx, %r8d
    addq %r8, %rax
    cmpq $VPP_ARENA_SIZE, %rax
    ja Lbo_nho_loi
    movq 3904(%r11), %rax
    testq %rax, %rax
    je Lbo_nho_loi
    leaq (%r11,%rcx), %rdx
    movq %rax, %rcx
    movl -24(%rbp), %r8d
    xorl %r9d, %r9d
    call BCryptHashData
    testl %eax, %eax
    js Lbo_nho_loi
    xorl %eax, %eax
    TRA_BO_NHO

.globl vpp_native_sha_end
vpp_native_sha_end:
    VAO_BO_NHO
    KIEM_TRA_LECH %ecx, 32
    movq 3904(%r11), %rax
    testq %rax, %rax
    je Lbo_nho_loi
    movq %r11, -32(%rbp)
    movl -4(%rbp), %edx
    movq %rax, %rcx
    leaq (%r11,%rdx), %rdx
    movl $32, %r8d
    xorl %r9d, %r9d
    call BCryptFinishHash
    movl %eax, -40(%rbp)
    movq -32(%rbp), %r10
    movq 3904(%r10), %rcx
    call BCryptDestroyHash
    movq -32(%rbp), %r10
    movq 3912(%r10), %rcx
    xorl %edx, %edx
    call BCryptCloseAlgorithmProvider
    movq -32(%rbp), %r10
    movq $0, 3904(%r10)
    movq $0, 3912(%r10)
    cmpl $0, -40(%rbp)
    js Lbo_nho_loi
    xorl %eax, %eax
    TRA_BO_NHO

