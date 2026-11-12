// Đăng ký và gọi báo cáo bộ nhớ đệm khi chương trình kết thúc.

.globl vpp_native_os_register_cache_report
vpp_native_os_register_cache_report:
    subq $40, %rsp
    leaq vpp_native_cache_report_entry(%rip), %rcx
    call atexit
    addq $40, %rsp
    ret

