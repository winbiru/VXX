// Điểm vào chương trình và khung ngăn xếp ban đầu.

#if defined(__linux__) && defined(VPP_LINUX_HOST_NO_CRT)
// Điểm vào bộ biên dịch khi bật VPP_LINUX_HOST_NO_CRT, không cần crt1.o hay __libc_start_main.
.text
.p2align 4
.globl _start
.type _start, @function
_start:
    xorq %rbp, %rbp
    movq %rsp, vpp_linux_host_initial_stack(%rip)
    movq (%rsp), %rdi
    leaq 8(%rsp), %rsi
    leaq 16(%rsp,%rdi,8), %rdx
    andq $-16, %rsp
    call main
    movl %eax, %ebx
// Chế độ này phải tự gọi hàm kết thúc thay cho CRT.
    call Llinux_chu_dem_bao_cao_ket_thuc
    movl %ebx, %edi
    movl $231, %eax             // SYS_exit_group
    syscall
    ud2
.size _start, .-_start
#endif

