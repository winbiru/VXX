// Phép chia i32 theo quy ước của bộ thực thi.

.globl PUBLIC(vpp_native_i32_div)
PUBLIC(vpp_native_i32_div):
    testl %esi, %esi
    je 1f
    cmpl $-1, %esi
    jne 2f
    cmpl $0x80000000, %edi
    je 3f
2:  movl %edi, %eax
    cltd
    idivl %esi
    ret
1:  movl $-1, %eax
    ret
3:  movl $0x80000000, %eax
    ret

