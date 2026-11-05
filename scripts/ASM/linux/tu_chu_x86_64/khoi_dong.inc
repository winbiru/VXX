// Điểm vào chương trình và khung ngăn xếp ban đầu.

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
    call Llinux_chu_dem_bao_cao_ket_thuc
    movl %ebx, %edi
    movl $231, %eax
    syscall
    ud2
.size _start, .-_start
