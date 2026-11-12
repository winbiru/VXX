// Đăng ký và gọi báo cáo bộ nhớ đệm khi chương trình kết thúc.

.globl vpp_native_os_register_cache_report
vpp_native_os_register_cache_report:
    str x30, [sp, #-16]!
    adrp x0, :got:vpp_native_cache_report_entry
    ldr x0, [x0, :got_lo12:vpp_native_cache_report_entry]
    bl atexit
    ldr x30, [sp], #16
    ret
