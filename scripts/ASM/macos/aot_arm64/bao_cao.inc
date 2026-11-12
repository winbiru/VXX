// Đăng ký và gọi báo cáo bộ nhớ đệm khi chương trình kết thúc.

// atexit gọi hàm báo cáo V++ đã xuất qua bộ chuyển đổi ABI C.
.globl _vpp_native_os_register_cache_report
_vpp_native_os_register_cache_report:
    str x30, [sp, #-16]!
    adrp x0, _vpp_native_cache_report_entry@PAGE
    add x0, x0, _vpp_native_cache_report_entry@PAGEOFF
    bl _atexit
    ldr x30, [sp], #16
    ret

