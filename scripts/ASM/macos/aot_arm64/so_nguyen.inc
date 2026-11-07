// Phép chia i32 theo quy ước của bộ thực thi.

.globl _vpp_native_i32_div
_vpp_native_i32_div:
    cbz w1, 1f
    sdiv w0, w0, w1
    ret
1:  mov w0, #-1
    ret

