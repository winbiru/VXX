// Hàm bọc nhập xuất và chuyển kết quả lời gọi hệ thống.

Llinux_goi_he_thong_ket_qua:
    cmpq $-4095, %rax
    jb 1f
    negl %eax
    movl %eax, vpp_linux_host_errno(%rip)
    movq $-1, %rax
1:
    ret
Llinux_doc:
    xorl %eax, %eax
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_ghi:
    movl $1, %eax
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_dong:
    movl $3, %eax
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_mo:
    movl %edx, %r10d
    movl %esi, %edx
    movq %rdi, %rsi
    movl $-100, %edi
    movl $257, %eax
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_stat:
    movq %rsi, %rdx
    movq %rdi, %rsi
    movq $-100, %rdi
    xorl %r10d, %r10d
    movl $262, %eax
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_lstat:
    movq %rsi, %rdx
    movq %rdi, %rsi
    movq $-100, %rdi
    movl $256, %r10d
    movl $262, %eax
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_stat_mo_ta:
    movl $5, %eax
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_truy_cap:
    movl %esi, %edx
    movq %rdi, %rsi
    movq $-100, %rdi
    xorl %r10d, %r10d
    movl $269, %eax
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_xoa_tep:
    movq %rdi, %rsi
    movq $-100, %rdi
    xorl %edx, %edx
    movl $263, %eax
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_tao_thu_muc:
    movl %esi, %edx
    movq %rdi, %rsi
    movq $-100, %rdi
    movl $258, %eax
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_doi_ten:
    movq %rsi, %r10
    movq $-100, %rdx
    movq %rdi, %rsi
    movq $-100, %rdi
    movl $264, %eax
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_doc_lien_ket:
    movq %rdx, %r10
    movq %rsi, %rdx
    movq %rdi, %rsi
    movq $-100, %rdi
    movl $267, %eax
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_lay_thu_muc:
    movl $79, %eax
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_doi_pid:
    xorl %r10d, %r10d
    movl $61, %eax
    syscall
    jmp Llinux_goi_he_thong_ket_qua
