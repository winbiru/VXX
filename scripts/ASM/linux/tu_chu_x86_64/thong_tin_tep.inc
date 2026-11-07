// Đọc định danh tệp và kiểm tra quyền truy cập.

.globl vpp_native_os_stat
vpp_native_os_stat:
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    cmpl $(3376), %esi
    je 1f
    cmpl $(3584), %esi
    jne Lbo_nho_loi
1:
    leaq (%r11,%rsi), %rsi
    movq %rsi, -32(%rbp)
    cmpl $(135271424 - 4096), %edi
    ja Lbo_nho_loi
    leaq (%r11,%rdi), %rdi
    call Llinux_stat
    testl %eax, %eax
    jne 2f
    movq -32(%rbp), %r10
    movl 24(%r10), %r9d
    movl %r9d, 4(%r10)
2:
    leave
    ret
.globl vpp_native_os_lstat
vpp_native_os_lstat:
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    cmpl $(3376), %esi
    je 1f
    cmpl $(3584), %esi
    jne Lbo_nho_loi
1:
    leaq (%r11,%rsi), %rsi
    movq %rsi, -32(%rbp)
    cmpl $(135271424 - 4096), %edi
    ja Lbo_nho_loi
    leaq (%r11,%rdi), %rdi
    call Llinux_lstat
    testl %eax, %eax
    jne 2f
    movq -32(%rbp), %r10
    movl 24(%r10), %r9d
    movl %r9d, 4(%r10)
2:
    leave
    ret
.globl vpp_native_os_fstat
vpp_native_os_fstat:
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    cmpl $(3376), %esi
    je 1f
    cmpl $(3584), %esi
    jne Lbo_nho_loi
1:
    leaq (%r11,%rsi), %rsi
    movq %rsi, -32(%rbp)
    call Llinux_stat_mo_ta
    testl %eax, %eax
    jne 2f
    movq -32(%rbp), %r10
    movl 24(%r10), %r9d
    movl %r9d, 4(%r10)
2:
    leave
    ret
.globl vpp_native_os_access_exec
vpp_native_os_access_exec:
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    cmpl $(135271424 - 4096), %edi
    ja Lbo_nho_loi
    leaq (%r11,%rdi), %rdi
    movl $1, %esi
    call Llinux_truy_cap
    leave
    ret
.globl vpp_native_os_fchmod
vpp_native_os_fchmod:
    movl $91, %eax
    syscall
    jmp Llinux_goi_he_thong_ket_qua
.globl vpp_native_os_unlink
vpp_native_os_unlink:
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    cmpl $(135271424 - 4096), %edi
    ja Lbo_nho_loi
    leaq (%r11,%rdi), %rdi
    call Llinux_xoa_tep
    leave
    ret
.globl vpp_native_os_mkdir
vpp_native_os_mkdir:
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lbo_nho_loi
    cmpl $(135271424 - 4096), %edi
    ja Lbo_nho_loi
    leaq (%r11,%rdi), %rdi
    movl $448, %esi
    call Llinux_tao_thu_muc
    leave
    ret
