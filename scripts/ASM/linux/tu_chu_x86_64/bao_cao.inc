// Đăng ký và gọi báo cáo bộ nhớ đệm khi chương trình kết thúc.

.globl vpp_native_os_register_cache_report
vpp_native_os_register_cache_report:
    movl $1, vpp_linux_host_cache_report_requested(%rip)
    xorl %eax, %eax
    ret
.p2align 4
Llinux_chu_dem_bao_cao_ket_thuc:
    xorl %eax, %eax
    xchgl %eax, vpp_linux_host_cache_report_requested(%rip)
    testl %eax, %eax
    je 1f
    subq $8, %rsp
    call vpp_native_cache_report_entry
    addq $8, %rsp
1:
    ret
.section .fini_array,"aw",@fini_array
.p2align 3
.quad Llinux_chu_dem_bao_cao_ket_thuc
.text
