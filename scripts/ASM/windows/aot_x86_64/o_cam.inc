// Winsock SOCKETs are pointer-sized. Guest controls use table indices;
// only this boundary ever reads or writes the native handles.
.text
.globl vpp_native_os_socket
vpp_native_os_socket:
    WIN_VAO
    cmpl $10, %r12d
    ja .Lwin_khong_hop_le
    cmpl $(VPP_ARENA_SIZE-128), %r13d
    ja .Lwin_khong_hop_le
    testl $15, %r13d
    jnz .Lwin_khong_hop_le
    leaq (%rbx,%r13), %r15
    cmpl $0, .Lwin_wsa_san_sang(%rip)
    jne 1f
    movl $0x202, %ecx
    leaq .Lwin_wsa_du_lieu(%rip), %rdx
    call WSAStartup
    testl %eax, %eax
    jnz .Lwin_socket_loi
    movl $1, .Lwin_wsa_san_sang(%rip)
1:  movl $0, 28(%r15)
    testl %r12d, %r12d
    jz .Lwin_socket_tao
    cmpl $1, %r12d
    je .Lwin_socket_tao
    cmpl $7, %r12d
    je .Lwin_socket_tao
    call .Lwin_socket_tim
    cmpq $-1, %rax
    je .Lwin_khong_hop_le
    movq %rax, %r14
    cmpl $2, %r12d
    je .Lwin_socket_nhan_khach
    cmpl $5, %r12d
    je .Lwin_socket_dong
    cmpl $6, %r12d
    je .Lwin_socket_timeout
    cmpl $10, %r12d
    je .Lwin_socket_cong
    movl 20(%r15), %r8d
    cmpl $1, %r8d
    jb .Lwin_khong_hop_le
    cmpl $65536, %r8d
    ja .Lwin_khong_hop_le
    movl 16(%r15), %edx
    movl %edx, %eax
    addq %r8, %rax
    cmpq $VPP_ARENA_SIZE, %rax
    ja .Lwin_khong_hop_le
    leaq (%rbx,%rdx), %rdx
    movq %r14, %rcx
    xorl %r9d, %r9d
    cmpl $3, %r12d
    je .Lwin_socket_gui
    cmpl $4, %r12d
    je .Lwin_socket_nhan
    leaq 48(%r15), %rax
    movq %rax, 32(%rsp)
    cmpl $8, %r12d
    je .Lwin_socket_nhan_goi
    cmpl $9, %r12d
    jne .Lwin_khong_hop_le
    movl 112(%r15), %eax
    movq %rax, 40(%rsp)
    call sendto
    jmp .Lwin_socket_io_xong
.Lwin_socket_nhan_goi:
    movl $32, 112(%r15)
    leaq 112(%r15), %rax
    movq %rax, 40(%rsp)
    call recvfrom
    jmp .Lwin_socket_io_xong
.Lwin_socket_gui:
    call send
    jmp .Lwin_socket_io_xong
.Lwin_socket_nhan:
    call recv
.Lwin_socket_io_xong:
    cmpl $-1, %eax
    je .Lwin_socket_loi
    WIN_RA
.Lwin_socket_tao:
    movl $2, %ecx
    cmpl $16, 24(%r15)
    jne 1f
    movl $23, %ecx
1:  movl %ecx, -72(%rbp)
    movl $1, %edx
    cmpl $7, %r12d
    je 2f
    cmpl $0, 4(%r15)
    je 3f
2:  movl $2, %edx
3:  xorl %r8d, %r8d
    xorl %r9d, %r9d
    movq $0, 32(%rsp)
    movq $0x80, 40(%rsp)
    call WSASocketW
    cmpq $-1, %rax
    je .Lwin_socket_loi
    movq %rax, %r14
    movq $0, 48(%r15)
    movq $0, 56(%r15)
    movq $0, 64(%r15)
    movq $0, 72(%r15)
    movl -72(%rbp), %eax
    movw %ax, 48(%r15)
    movl 8(%r15), %eax
    rolw $8, %ax
    movw %ax, 50(%r15)
    movl $16, 112(%r15)
    cmpl $23, -72(%rbp)
    jne 1f
    movq 32(%r15), %rax
    movq %rax, 56(%r15)
    movq 40(%r15), %rax
    movq %rax, 64(%r15)
    movl $28, 112(%r15)
    jmp 2f
1:  movl 32(%r15), %eax
    movl %eax, 52(%r15)
2:  call .Lwin_socket_dat_timeout
    testl %eax, %eax
    jnz .Lwin_socket_tao_loi
    testl %r12d, %r12d
    jz .Lwin_socket_ket_noi
    movq %r14, %rcx
    leaq 48(%r15), %rdx
    movl 112(%r15), %r8d
    call bind
    testl %eax, %eax
    jnz .Lwin_socket_tao_loi
    cmpl $7, %r12d
    je .Lwin_socket_dang_ky
    movq %r14, %rcx
    movl 20(%r15), %edx
    call listen
    testl %eax, %eax
    jnz .Lwin_socket_tao_loi
    jmp .Lwin_socket_dang_ky
.Lwin_socket_ket_noi:
    movl $1, -80(%rbp)
    movq %r14, %rcx
    movl $0x8004667e, %edx
    leaq -80(%rbp), %r8
    call ioctlsocket
    testl %eax, %eax
    jnz .Lwin_socket_tao_loi
    movq %r14, %rcx
    leaq 48(%r15), %rdx
    movl 112(%r15), %r8d
    call connect
    testl %eax, %eax
    jz .Lwin_socket_chan
    call WSAGetLastError
    cmpl $10035, %eax
    jne .Lwin_socket_tao_loi
    movl $0x10, %edx
    call .Lwin_socket_poll
    cmpl $1, %eax
    jne .Lwin_socket_tao_loi
    movq %r14, %rcx
    movl $65535, %edx
    movl $0x1007, %r8d
    leaq -80(%rbp), %r9
    movl $4, -88(%rbp)
    leaq -88(%rbp), %rax
    movq %rax, 32(%rsp)
    call getsockopt
    testl %eax, %eax
    jnz .Lwin_socket_tao_loi
    cmpl $0, -80(%rbp)
    jne .Lwin_socket_tao_loi
.Lwin_socket_chan:
    movl $0, -80(%rbp)
    movq %r14, %rcx
    movl $0x8004667e, %edx
    leaq -80(%rbp), %r8
    call ioctlsocket
    testl %eax, %eax
    jnz .Lwin_socket_tao_loi
.Lwin_socket_dang_ky:
    leaq .Lwin_sockets(%rip), %rdx
    movl $1, %eax
1:  cmpq $0, (%rdx,%rax,8)
    je 2f
    incl %eax
    cmpl $1024, %eax
    jb 1b
    jmp .Lwin_socket_tao_loi
2:  leaq 1(%r14), %rcx
    movq %rcx, (%rdx,%rax,8)
    movl %eax, (%r15)
    WIN_RA
.Lwin_socket_nhan_khach:
    movl $0x100, %edx
    call .Lwin_socket_poll
    cmpl $1, %eax
    jne .Lwin_socket_loi
    movq %r14, %rcx
    xorl %edx, %edx
    xorl %r8d, %r8d
    call accept
    cmpq $-1, %rax
    je .Lwin_socket_loi
    movq %rax, %r14
    call .Lwin_socket_dat_timeout
    testl %eax, %eax
    jnz .Lwin_socket_tao_loi
    jmp .Lwin_socket_dang_ky
.Lwin_socket_timeout:
    call .Lwin_socket_dat_timeout
    testl %eax, %eax
    jnz .Lwin_socket_loi
    xorl %eax, %eax
    WIN_RA
.Lwin_socket_dat_timeout:
    subq $56, %rsp
    movl 12(%r15), %eax
    decl %eax
    cmpl $299999, %eax
    ja 2f
    movq %r14, %rcx
    movl $65535, %edx
    movl $0x1006, %r8d
    leaq 12(%r15), %r9
    movq $4, 32(%rsp)
    call setsockopt
    testl %eax, %eax
    jnz 1f
    movq %r14, %rcx
    movl $65535, %edx
    movl $0x1005, %r8d
    leaq 12(%r15), %r9
    movq $4, 32(%rsp)
    call setsockopt
1:  addq $56, %rsp
    ret
2:  movl $-1, %eax
    jmp 1b
.Lwin_socket_poll:
    subq $56, %rsp
    movq %r14, 32(%rsp)
    movw %dx, 40(%rsp)
    movw $0, 42(%rsp)
    leaq 32(%rsp), %rcx
    movl $1, %edx
    movl 12(%r15), %r8d
    call WSAPoll
    addq $56, %rsp
    ret
.Lwin_socket_tim:
    movl (%r15), %eax
    decl %eax
    cmpl $1022, %eax
    ja 1f
    incl %eax
    leaq .Lwin_sockets(%rip), %rcx
    movq (%rcx,%rax,8), %rax
    testq %rax, %rax
    je 1f
    decq %rax
    ret
1:  movq $-1, %rax
    ret
.Lwin_socket_cong:
    movq %r14, %rcx
    leaq 48(%r15), %rdx
    movl $32, 112(%r15)
    leaq 112(%r15), %r8
    call getsockname
    testl %eax, %eax
    jnz .Lwin_socket_loi
    movzwl 50(%r15), %eax
    rolw $8, %ax
    movl %eax, 8(%r15)
    WIN_RA
.Lwin_socket_dong:
    movq %r14, %rcx
    call closesocket
    movl %eax, %r12d
    movl (%r15), %eax
    leaq .Lwin_sockets(%rip), %rdx
    movq $0, (%rdx,%rax,8)
    movl $-1, (%r15)
    testl %r12d, %r12d
    jnz .Lwin_socket_loi
    xorl %eax, %eax
    WIN_RA
.Lwin_socket_tao_loi:
    call WSAGetLastError
    movl %eax, -96(%rbp)
    movq %r14, %rcx
    call closesocket
    movl -96(%rbp), %eax
    jmp .Lwin_socket_doi_loi
.Lwin_socket_loi:
    call WSAGetLastError
.Lwin_socket_doi_loi:
    movl $110, %edx
    cmpl $10060, %eax
    je 1f
    movl $11, %edx
    cmpl $10035, %eax
    je 1f
    movl $4, %edx
    cmpl $10004, %eax
    je 1f
    movl $111, %edx
    cmpl $10061, %eax
    je 1f
    movl $5, %edx
1:  movl %edx, 28(%r15)
    movl %edx, %eax
    negl %eax
    WIN_RA
.bss
.p2align 3
.Lwin_sockets: .zero 8192
.Lwin_wsa_san_sang: .zero 4
.Lwin_wsa_du_lieu: .zero 512
.text
