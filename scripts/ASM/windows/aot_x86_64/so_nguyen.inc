// Phép chia i32 theo quy ước của bộ thực thi.

.globl vpp_native_i32_div
vpp_native_i32_div:
    testl %edx, %edx
    je 1f
    cmpl $-1, %edx
    jne 2f
    cmpl $0x80000000, %ecx
    je 3f
2:  movl %ecx, %eax
    movl %edx, %ecx
    cltd
    idivl %ecx
    ret
1:  movl $-1, %eax
    ret
3:  movl $0x80000000, %eax
    ret

