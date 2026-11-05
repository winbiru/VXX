// Thao tác ổ cắm TCP/UDP và thời gian chờ.

.text
.globl vpp_native_os_socket
vpp_native_os_socket:
    pushq %rbp
    movq %rsp, %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    subq $136, %rsp
    movl %edi, %ebx
    movl %esi, %r13d
    call vpp_native_memory_base
    testq %rax, %rax
    je .Lo_cam_khong_hop_le_truoc_x
    cmpl $10, %ebx
    ja .Lo_cam_khong_hop_le_truoc_x
    cmpl $(135271424-128), %r13d
    ja .Lo_cam_khong_hop_le_truoc_x
    testl $15, %r13d
    jnz .Lo_cam_khong_hop_le_truoc_x
    leaq (%rax,%r13), %r12
    movl $0, 28(%r12)
    cmpl $7, %ebx
    je .Lo_cam_udp_gan_x
    cmpl $8, %ebx
    je .Lo_cam_nhap_xuat_x
    cmpl $9, %ebx
    je .Lo_cam_nhap_xuat_x
    cmpl $10, %ebx
    je .Lo_cam_cong_x
    cmpl $0, %ebx
    je .Lo_cam_ket_noi_x
    cmpl $1, %ebx
    je .Lo_cam_lang_nghe_x
    cmpl $2, %ebx
    je .Lo_cam_chap_nhan_x
    cmpl $3, %ebx
    je .Lo_cam_nhap_xuat_x
    cmpl $4, %ebx
    je .Lo_cam_nhap_xuat_x
    cmpl $5, %ebx
    je .Lo_cam_dong_x
    jmp .Lo_cam_thoi_gian_cho_x
.Lo_cam_dia_chi_x:
    movl %edi, %edx
    leaq 48(%r12), %rdi
    xorl %eax, %eax
    movl $32, %ecx
    rep stosb
    movw %dx, 48(%r12)
    movl 8(%r12), %eax
    rolw $8, %ax
    movw %ax, 50(%r12)
    cmpl $2, %edx
    jne 3f
    movl 32(%r12), %eax
    movl %eax, 52(%r12)
    ret
3:
    movq 32(%r12), %rax
    movq %rax, 56(%r12)
    movq 40(%r12), %rax
    movq %rax, 64(%r12)
    ret
.Lo_cam_udp_gan_x:
    movl 8(%r12), %eax
    testl %eax, %eax
    js .Lo_cam_khong_hop_le_x
    cmpl $65535, %eax
    jg .Lo_cam_khong_hop_le_x
    movl 12(%r12), %eax
    testl %eax, %eax
    jle .Lo_cam_khong_hop_le_x
    cmpl $300000, %eax
    jg .Lo_cam_khong_hop_le_x
    movl $2, %edi
    call .Lo_cam_dia_chi_x
    movl $2, %edi
    movl $2, %esi
    xorl %edx, %edx
    call vpp_sys_socket
    testl %eax, %eax
    js .Lo_cam_loi_tra_ve_x
    movl %eax, (%r12)
    call .Lo_cam_dat_thoi_gian_cho_x
    testl %eax, %eax
    js .Lo_cam_dong_loi_x
    movl (%r12), %edi
    leaq 48(%r12), %rsi
    movl $16, %edx
    call vpp_sys_bind
    testl %eax, %eax
    js .Lo_cam_errno_dong_x
    movl (%r12), %eax
    jmp .Lo_cam_tra_ve_x
.Lo_cam_cong_x:
    movl (%r12), %edi
    testl %edi, %edi
    js .Lo_cam_khong_hop_le_x
    movl $32, 112(%r12)
    leaq 48(%r12), %rsi
    leaq 112(%r12), %rdx
    call vpp_sys_getsockname
    testl %eax, %eax
    js .Lo_cam_loi_tra_ve_x
    movzwl 50(%r12), %eax
    rolw $8, %ax
    movl %eax, 8(%r12)
    jmp .Lo_cam_tra_ve_x
.Lo_cam_ket_noi_x:
    movl 24(%r12), %eax
    cmpl $4, %eax
    je 1f
    cmpl $16, %eax
    jne .Lo_cam_khong_hop_le_x
1:
    movl 8(%r12), %eax
    testl %eax, %eax
    jle .Lo_cam_khong_hop_le_x
    cmpl $65535, %eax
    jg .Lo_cam_khong_hop_le_x
    cmpl $1, 4(%r12)
    ja .Lo_cam_khong_hop_le_x
    movl $2, %edi
    cmpl $16, 24(%r12)
    jne 2f
    movl $10, %edi
2:
    movl %edi, %ebx
    call .Lo_cam_dia_chi_x
    movl %ebx, %edi
    movl $1, %esi
    cmpl $1, 4(%r12)
    jne 3f
    movl $2, %esi
3:
    xorl %edx, %edx
    call vpp_sys_socket
    testl %eax, %eax
    js .Lo_cam_loi_tra_ve_x
    movl %eax, 0(%r12)
    call .Lo_cam_khong_sigpipe_x
    testl %eax, %eax
    js .Lo_cam_dong_loi_x
    movl 12(%r12), %eax
    testl %eax, %eax
    jle .Lo_cam_khong_hop_le_mo_ta_x
    cmpl $300000, %eax
    jg .Lo_cam_khong_hop_le_mo_ta_x
    call .Lo_cam_dat_thoi_gian_cho_x
    testl %eax, %eax
    js .Lo_cam_dong_loi_x
    movl (%r12), %edi
    movl $3, %esi
    xorl %eax, %eax
    call vpp_sys_fcntl
    testl %eax, %eax
    js .Lo_cam_errno_dong_x
    movl %eax, 104(%r12)
    orl $2048, %eax
    movl %eax, %edx
    movl (%r12), %edi
    movl $4, %esi
    xorl %eax, %eax
    call vpp_sys_fcntl
    testl %eax, %eax
    js .Lo_cam_errno_dong_x
    movl 0(%r12), %edi
    leaq 48(%r12), %rsi
    movl $16, %edx
    cmpl $16, 24(%r12)
    jne 4f
    movl $28, %edx
4:
    call vpp_sys_connect
    testl %eax, %eax
    jns .Lo_cam_da_noi_x
    call .Lo_cam_errno_x
    cmpl $-115, %eax
    je .Lo_cam_ket_noi_cho_x
    cmpl $-4, %eax
    jne .Lo_cam_ket_noi_khoi_phuc_loi_x
.Lo_cam_ket_noi_cho_x:
    movl (%r12), %eax
    movl %eax, 96(%r12)
    movw $4, 100(%r12)
    movw $0, 102(%r12)
    call .Lo_cam_han_khoi_dong_x
    testl %eax, %eax
    js .Lo_cam_ket_noi_khoi_phuc_loi_x
.Lo_cam_ket_noi_tham_do_lap_x:
    call .Lo_cam_han_con_lai_x
    testl %eax, %eax
    js .Lo_cam_ket_noi_khoi_phuc_loi_x
    jz .Lo_cam_ket_noi_qua_han_x
    movl %eax, %edx
    leaq 96(%r12), %rdi
    movl $1, %esi
    call vpp_sys_poll
    testl %eax, %eax
    js .Lo_cam_ket_noi_tham_do_loi_x
    jz .Lo_cam_ket_noi_qua_han_x
    movl (%r12), %edi
    movl $1, %esi
    movl $4, %edx
    leaq 108(%r12), %rcx
    movl $4, 112(%r12)
    leaq 112(%r12), %r8
    call vpp_sys_getsockopt
    testl %eax, %eax
    js .Lo_cam_ket_noi_tham_do_loi_x
    movl 108(%r12), %eax
    testl %eax, %eax
    jz .Lo_cam_da_noi_x
    movl %eax, 28(%r12)
    negl %eax
    jmp .Lo_cam_ket_noi_khoi_phuc_loi_x
.Lo_cam_ket_noi_qua_han_x:
    movl $110, 28(%r12)
    movl $-110, %eax
    jmp .Lo_cam_ket_noi_khoi_phuc_loi_x
.Lo_cam_ket_noi_tham_do_loi_x:
    call .Lo_cam_errno_x
    cmpl $-4, %eax
    je .Lo_cam_ket_noi_tham_do_lap_x
.Lo_cam_ket_noi_khoi_phuc_loi_x:
    movl %eax, %ebx
    movl (%r12), %edi
    movl $4, %esi
    movl 104(%r12), %edx
    xorl %eax, %eax
    call vpp_sys_fcntl
    movl %ebx, %eax
    jmp .Lo_cam_dong_loi_x
.Lo_cam_da_noi_x:
    movl $0, 28(%r12)
    movl (%r12), %edi
    movl $4, %esi
    movl 104(%r12), %edx
    xorl %eax, %eax
    call vpp_sys_fcntl
    testl %eax, %eax
    js .Lo_cam_errno_dong_x
    movl 0(%r12), %eax
    jmp .Lo_cam_tra_ve_x
.Lo_cam_lang_nghe_x:
    movl 8(%r12), %eax
    testl %eax, %eax
    js .Lo_cam_khong_hop_le_x
    cmpl $65535, %eax
    jg .Lo_cam_khong_hop_le_x
    movl 20(%r12), %eax
    testl %eax, %eax
    jle .Lo_cam_khong_hop_le_x
    cmpl $1024, %eax
    jg .Lo_cam_khong_hop_le_x
    movl $2, %edi
    call .Lo_cam_dia_chi_x
    movl $2, %edi
    movl $1, %esi
    xorl %edx, %edx
    call vpp_sys_socket
    testl %eax, %eax
    js .Lo_cam_loi_tra_ve_x
    movl %eax, (%r12)
    movl $1, 96(%r12)
    movl (%r12), %edi
    movl $1, %esi
    movl $2, %edx
    leaq 96(%r12), %rcx
    movl $4, %r8d
    call vpp_sys_setsockopt
    testl %eax, %eax
    js .Lo_cam_errno_dong_x
    movl (%r12), %edi
    leaq 48(%r12), %rsi
    movl $16, %edx
    call vpp_sys_bind
    testl %eax, %eax
    js .Lo_cam_errno_dong_x
    movl (%r12), %edi
    movl 20(%r12), %esi
    call vpp_sys_listen
    testl %eax, %eax
    js .Lo_cam_errno_dong_x
    movl (%r12), %eax
    jmp .Lo_cam_tra_ve_x
.Lo_cam_chap_nhan_x:
    movl (%r12), %eax
    testl %eax, %eax
    js .Lo_cam_khong_hop_le_x
    movl %eax, 96(%r12)
    movw $1, 100(%r12)
    movw $0, 102(%r12)
    movl 12(%r12), %edx
    testl %edx, %edx
    jle .Lo_cam_khong_hop_le_x
    call .Lo_cam_han_khoi_dong_x
    testl %eax, %eax
    js .Lo_cam_tra_ve_x
.Lo_cam_chap_nhan_tham_do_lap_x:
    call .Lo_cam_han_con_lai_x
    testl %eax, %eax
    js .Lo_cam_tra_ve_x
    jz .Lo_cam_chap_nhan_qua_han_x
    movl %eax, %edx
    leaq 96(%r12), %rdi
    movl $1, %esi
    call vpp_sys_poll
    testl %eax, %eax
    js .Lo_cam_chap_nhan_tham_do_loi_x
    jz .Lo_cam_chap_nhan_qua_han_x
    movl (%r12), %edi
    xorl %esi, %esi
    xorl %edx, %edx
    call vpp_sys_accept
    testl %eax, %eax
    js .Lo_cam_loi_tra_ve_x
    movl %eax, (%r12)
    call .Lo_cam_khong_sigpipe_x
    testl %eax, %eax
    js .Lo_cam_dong_loi_x
    call .Lo_cam_dat_thoi_gian_cho_x
    testl %eax, %eax
    js .Lo_cam_dong_loi_x
    movl (%r12), %eax
    jmp .Lo_cam_tra_ve_x
.Lo_cam_chap_nhan_qua_han_x:
    movl $110, 28(%r12)
    movl $-110, %eax
    jmp .Lo_cam_tra_ve_x
.Lo_cam_chap_nhan_tham_do_loi_x:
    call .Lo_cam_errno_x
    cmpl $-4, %eax
    je .Lo_cam_chap_nhan_tham_do_lap_x
    jmp .Lo_cam_tra_ve_x
.Lo_cam_nhap_xuat_x:
    movl 16(%r12), %esi
    movl 20(%r12), %edx
    testl %edx, %edx
    jle .Lo_cam_khong_hop_le_x
    cmpl $65536, %edx
    jg .Lo_cam_khong_hop_le_x
    cmpl $135271424, %esi
    jae .Lo_cam_khong_hop_le_x
    movl %esi, %eax
    addq %rax, %rdx
    cmpq $135271424, %rdx
    ja .Lo_cam_khong_hop_le_x
    movl %esi, %r13d
    call vpp_native_memory_base
    leaq (%rax,%r13), %rsi
    movl (%r12), %edi
    movl 20(%r12), %edx
    movl $0x4000, %ecx
    cmpl $8, %ebx
    je .Lo_cam_nhan_goi_x
    cmpl $9, %ebx
    je .Lo_cam_gui_goi_x
    cmpl $3, %ebx
    jne 1f
    call vpp_sys_send
    jmp 2f
1:
    xorl %ecx, %ecx
    call vpp_sys_recv
2:
    testl %eax, %eax
    js .Lo_cam_loi_tra_ve_x
    jmp .Lo_cam_tra_ve_x
.Lo_cam_nhan_goi_x:
    xorl %ecx, %ecx
    movl $32, 112(%r12)
    leaq 48(%r12), %r8
    leaq 112(%r12), %r9
    call vpp_sys_recvfrom
    testl %eax, %eax
    js .Lo_cam_loi_tra_ve_x
    jmp .Lo_cam_tra_ve_x
.Lo_cam_gui_goi_x:
    cmpl $16, 112(%r12)
    jne .Lo_cam_khong_hop_le_x
    leaq 48(%r12), %r8
    movl $16, %r9d
    call vpp_sys_sendto
    testl %eax, %eax
    js .Lo_cam_loi_tra_ve_x
    jmp .Lo_cam_tra_ve_x
.Lo_cam_dong_x:
    movl (%r12), %edi
    testl %edi, %edi
    js .Lo_cam_khong_hop_le_x
    call vpp_sys_close
    testl %eax, %eax
    js .Lo_cam_loi_tra_ve_x
    movl $-1, (%r12)
    xorl %eax, %eax
    jmp .Lo_cam_tra_ve_x
.Lo_cam_thoi_gian_cho_x:
    movl (%r12), %edi
    testl %edi, %edi
    js .Lo_cam_khong_hop_le_x
    movl 12(%r12), %eax
    testl %eax, %eax
    jle .Lo_cam_khong_hop_le_x
    cmpl $300000, %eax
    jg .Lo_cam_khong_hop_le_x
    call .Lo_cam_dat_thoi_gian_cho_x
    jmp .Lo_cam_tra_ve_x
.Lo_cam_dat_thoi_gian_cho_x:
    subq $8, %rsp
    movl 12(%r12), %eax
    xorl %edx, %edx
    movl $1000, %ecx
    divl %ecx
    movq %rax, 80(%r12)
    imull $1000, %edx, %edx
    movq %rdx, 88(%r12)
    movl (%r12), %edi
    movl $1, %esi
    movl $20, %edx
    leaq 80(%r12), %rcx
    movl $16, %r8d
    call vpp_sys_setsockopt
    testl %eax, %eax
    js .Lo_cam_thoi_gian_cho_loi_x
    movl (%r12), %edi
    movl $1, %esi
    movl $21, %edx
    leaq 80(%r12), %rcx
    movl $16, %r8d
    call vpp_sys_setsockopt
    testl %eax, %eax
    js .Lo_cam_thoi_gian_cho_loi_x
    xorl %eax, %eax
    addq $8, %rsp
    ret
.Lo_cam_thoi_gian_cho_loi_x:
    call .Lo_cam_errno_x
    addq $8, %rsp
    ret
.Lo_cam_khong_sigpipe_x:
    xorl %eax, %eax
    ret
.Lo_cam_han_khoi_dong_x:
    subq $8, %rsp
    movl $1, %edi
    leaq 64(%rsp), %rsi
    call vpp_sys_clock_gettime
    testl %eax, %eax
    js .Lo_cam_han_errno_x
    call .Lo_cam_hien_tai_mili_giay_x
    movl 12(%r12), %ecx
    addq %rcx, %rax
    movq %rax, 96(%rsp)
    xorl %eax, %eax
    jmp .Lo_cam_han_ve_x
.Lo_cam_han_con_lai_x:
    subq $8, %rsp
    movl $1, %edi
    leaq 64(%rsp), %rsi
    call vpp_sys_clock_gettime
    testl %eax, %eax
    js .Lo_cam_han_errno_x
    call .Lo_cam_hien_tai_mili_giay_x
    movq 96(%rsp), %rcx
    subq %rax, %rcx
    jle .Lo_cam_han_het_han_x
    movl %ecx, %eax
    jmp .Lo_cam_han_ve_x
.Lo_cam_han_het_han_x:
    xorl %eax, %eax
    jmp .Lo_cam_han_ve_x
.Lo_cam_han_errno_x:
    call .Lo_cam_errno_x
.Lo_cam_han_ve_x:
    addq $8, %rsp
    ret
.Lo_cam_hien_tai_mili_giay_x:
    movq 80(%rsp), %rax
    xorl %edx, %edx
    movq $1000000, %rcx
    divq %rcx
    movq 72(%rsp), %rcx
    imulq $1000, %rcx, %rcx
    addq %rcx, %rax
    ret
.Lo_cam_dong_loi_x:
    movl %eax, %ebx
    movl (%r12), %edi
    call vpp_sys_close
    movl $-1, (%r12)
    movl %ebx, %eax
    jmp .Lo_cam_tra_ve_x
.Lo_cam_errno_dong_x:
    call .Lo_cam_errno_x
    jmp .Lo_cam_dong_loi_x
.Lo_cam_loi_tra_ve_x:
    call .Lo_cam_errno_x
    jmp .Lo_cam_tra_ve_x
.Lo_cam_khong_hop_le_mo_ta_x:
    movl $22, 28(%r12)
    movl $-22, %eax
    jmp .Lo_cam_dong_loi_x
.Lo_cam_errno_x:
    movl %eax, %ecx
    negl %ecx
    movl %ecx, 28(%r12)
    ret
.Lo_cam_khong_hop_le_x:
    movl $22, 28(%r12)
.Lo_cam_khong_hop_le_truoc_x:
    movl $-22, %eax
.Lo_cam_tra_ve_x:
    addq $136, %rsp
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.size vpp_native_os_socket, .-vpp_native_os_socket
.section .note.GNU-stack,"",@progbits
