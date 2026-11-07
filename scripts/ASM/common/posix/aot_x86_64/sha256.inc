// Khởi tạo, cập nhật và kết thúc phép băm SHA-256.

// macOS dùng CommonCrypto. Bộ biên dịch và bộ thực thi Linux dùng chung mã SHA-256 độc lập được chèn
// từ tệp .S của Linux.
#ifdef __APPLE__
.globl PUBLIC(vpp_native_sha_begin)
PUBLIC(vpp_native_sha_begin):
    VAO_BO_NHO
    leaq 3904(%r11), %rdi
    call LIB(CC_SHA256_Init)
    cmpl $1, %eax
    je 1f
    movl $-1, %eax
    TRA_BO_NHO
1:  xorl %eax, %eax
    TRA_BO_NHO
.globl PUBLIC(vpp_native_sha_update)
PUBLIC(vpp_native_sha_update):
    VAO_BO_NHO
    KIEM_TRA_LECH %edi, 0
    cmpl $0, %esi
    jl Lbo_nho_loi
    movl %edi, %eax
    addq %rsi, %rax
    cmpq $VPP_ARENA_SIZE, %rax
    ja Lbo_nho_loi
    movl %esi, %edx
    leaq (%r11,%rdi), %rsi
    leaq 3904(%r11), %rdi
    call LIB(CC_SHA256_Update)
    cmpl $1, %eax
    jne Lsha_that_bai
    xorl %eax, %eax
    TRA_BO_NHO
.globl PUBLIC(vpp_native_sha_end)
PUBLIC(vpp_native_sha_end):
    VAO_BO_NHO
    KIEM_TRA_LECH %edi, 32
    leaq 3904(%r11), %rsi
    leaq (%r11,%rdi), %rdi
    call LIB(CC_SHA256_Final)
    cmpl $1, %eax
    jne Lsha_that_bai
    xorl %eax, %eax
    TRA_BO_NHO
Lsha_that_bai:
    movl $-1, %eax
    TRA_BO_NHO
#endif // __APPLE__

