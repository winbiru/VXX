// ABI máy cho tệp thô.vi: (số syscall, a, b, c, d, errno_out) trả kết quả 64 bit có dấu. Bộ chuyển
// đổi VM/AOT lấy đối số từ giá trị và vùng đệm do V++ sở hữu.
#if defined(__APPLE__)
#define FILE_SYSCALL _vpp_machine_file_syscall4
#define SOCKET_OPTION_SYSCALL _vpp_machine_socket_syscall5
#define SOCKET_IO_SYSCALL _vpp_machine_socket_syscall6
#else
#define FILE_SYSCALL vpp_machine_file_syscall4
#define SOCKET_OPTION_SYSCALL vpp_machine_socket_syscall5
#define SOCKET_IO_SYSCALL vpp_machine_socket_syscall6
#endif
.text
.globl FILE_SYSCALL
.p2align 2
FILE_SYSCALL:
#if defined(__aarch64__)
    mov x9, x5
#if defined(__APPLE__)
    mov x16, x0
#else
    mov x8, x0
#endif
    mov x0, x1
    mov x1, x2
    mov x2, x3
    mov x3, x4
#if defined(__APPLE__)
    svc #0x80
    b.cc Ltep_thanh_cong
    str w0, [x9]
#else
    svc #0
    cmn x0, #4095
    b.cc Ltep_thanh_cong
    neg w10, w0
    str w10, [x9]
#endif
    mov x0, #-1
    ret
Ltep_thanh_cong:
    str wzr, [x9]
    ret
#elif defined(__x86_64__)
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    movq %r8, %r10
#if defined(__APPLE__)
    orq $0x2000000, %rax
    syscall
    jnc Ltep_thanh_cong
#else
    syscall
    cmpq $-4095, %rax
    jb Ltep_thanh_cong
    negl %eax
#endif
    movl %eax, (%r9)
    movq $-1, %rax
    ret
Ltep_thanh_cong:
    movl $0, (%r9)
    ret
#else
#error ABI máy tệp V++ chưa được hỗ trợ
#endif

// ABI năm đối số: (số syscall, a, b, c, d, e, errno_out) trả kết quả 64 bit có dấu. setsockopt dùng
// cùng quy ước lỗi với syscall4.
.globl SOCKET_OPTION_SYSCALL
.p2align 2
SOCKET_OPTION_SYSCALL:
#if defined(__aarch64__)
    mov x9, x6
#if defined(__APPLE__)
    mov x16, x0
#else
    mov x8, x0
#endif
    mov x0, x1
    mov x1, x2
    mov x2, x3
    mov x3, x4
    mov x4, x5
#if defined(__APPLE__)
    svc #0x80
    b.cc Lo_cam_tuy_chon_thanh_cong
    str w0, [x9]
#else
    svc #0
    cmn x0, #4095
    b.cc Lo_cam_tuy_chon_thanh_cong
    neg w10, w0
    str w10, [x9]
#endif
    mov x0, #-1
    ret
Lo_cam_tuy_chon_thanh_cong:
    str wzr, [x9]
    ret
#elif defined(__x86_64__)
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    movq %r8, %r10
    movq %r9, %r8
#if defined(__APPLE__)
    orq $0x2000000, %rax
    syscall
    jnc Lo_cam_tuy_chon_thanh_cong
#else
    syscall
    cmpq $-4095, %rax
    jb Lo_cam_tuy_chon_thanh_cong
    negl %eax
#endif
    movq 8(%rsp), %r9
    movl %eax, (%r9)
    movq $-1, %rax
    ret
Lo_cam_tuy_chon_thanh_cong:
    movq 8(%rsp), %r9
    movl $0, (%r9)
    ret
#else
#error ABI máy ổ cắm V++ chưa được hỗ trợ
#endif

// ABI sáu đối số: (số syscall, a, b, c, d, e, f, errno_out) trả kết quả 64 bit có dấu.
// sendto/recvfrom truyền đủ sáu đối số cho nhân.
.globl SOCKET_IO_SYSCALL
.p2align 2
SOCKET_IO_SYSCALL:
#if defined(__aarch64__)
    mov x9, x7
#if defined(__APPLE__)
    mov x16, x0
#else
    mov x8, x0
#endif
    mov x0, x1
    mov x1, x2
    mov x2, x3
    mov x3, x4
    mov x4, x5
    mov x5, x6
#if defined(__APPLE__)
    svc #0x80
    b.cc Lo_cam_nhap_xuat_thanh_cong
    str w0, [x9]
#else
    svc #0
    cmn x0, #4095
    b.cc Lo_cam_nhap_xuat_thanh_cong
    neg w10, w0
    str w10, [x9]
#endif
    mov x0, #-1
    ret
Lo_cam_nhap_xuat_thanh_cong:
    str wzr, [x9]
    ret
#elif defined(__x86_64__)
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    movq %r8, %r10
    movq %r9, %r8
    movq 8(%rsp), %r9
#if defined(__APPLE__)
    orq $0x2000000, %rax
    syscall
    jnc Lo_cam_nhap_xuat_thanh_cong
#else
    syscall
    cmpq $-4095, %rax
    jb Lo_cam_nhap_xuat_thanh_cong
    negl %eax
#endif
    movq 16(%rsp), %r9
    movl %eax, (%r9)
    movq $-1, %rax
    ret
Lo_cam_nhap_xuat_thanh_cong:
    movq 16(%rsp), %r9
    movl $0, (%r9)
    ret
#else
#error ABI máy ổ cắm V++ chưa được hỗ trợ
#endif
#if defined(__linux__)
#if defined(__aarch64__)
.section .note.GNU-stack,"",%progbits
#else
.section .note.GNU-stack,"",@progbits
#endif
#endif
