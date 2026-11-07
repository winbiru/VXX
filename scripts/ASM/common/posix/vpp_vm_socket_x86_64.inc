// ABI ổ cắm POSIX x86-64 nhận con trỏ từ độ lệch đã kiểm tra. V++ quản lý mã tài nguyên, kiểu và
// quyền sở hữu. Khối điều khiển: fd@0, loại@4, cổng@8, thời gian chờ@12, dữ liệu@16, số byte@20, số
// byte IP@24, errno@28, IP@32..47, sockaddr@48..79, timeval@80..95, pollfd@96, cờ cũ@104, lỗi kết
// nối@108, độ dài sockaddr@112. Lệnh 7/8/9/10 phục vụ gắn UDP, nhận gói, gửi gói và lấy cổng cục bộ.
// Thời hạn đơn điệu nằm trên ngăn xếp riêng.
#ifdef __APPLE__
#define SOCK_SYM(x) _##x
#define SOCK_LIB(x) _##x
#define SOCK_LEVEL 0xffff
#define SOCK_REUSE 4
#define SOCK_RCVTIMEO 0x1006
#define SOCK_SNDTIMEO 0x1005
#define SOCK_NOSIGPIPE 0x1022
#define SOCK_MSGFLAGS 0
#define SOCK_INET6 30
#define SOCK_NONBLOCK 4
#define SOCK_INPROGRESS 36
#define SOCK_TIMEDOUT 60
#define SOCK_SOERROR 0x1007
#define SOCK_CLOCK 6
#else
#define SOCK_SYM(x) x
#define SOCK_LIB(x) x@PLT
#define SOCK_LEVEL 1
#define SOCK_REUSE 2
#define SOCK_RCVTIMEO 20
#define SOCK_SNDTIMEO 21
#define SOCK_MSGFLAGS 0x4000
#define SOCK_INET6 10
#define SOCK_NONBLOCK 2048
#define SOCK_INPROGRESS 115
#define SOCK_TIMEDOUT 110
#define SOCK_SOERROR 4
#define SOCK_CLOCK 1
#endif
.text
.globl SOCK_SYM(vpp_native_os_socket)
SOCK_SYM(vpp_native_os_socket):
    pushq %rbp
    movq %rsp, %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    subq $136, %rsp
    movl %edi, %ebx                  // Lệnh thao tác
    movl %esi, %r13d                 // Độ lệch khối điều khiển
    call SOCK_SYM(vpp_native_memory_base)
    testq %rax, %rax
    je .Lo_cam_khong_hop_le_x
    cmpl $10, %ebx
    ja .Lo_cam_khong_hop_le_x
    cmpl $(68162560-128), %r13d
    ja .Lo_cam_khong_hop_le_x
    testl $15, %r13d
    jnz .Lo_cam_khong_hop_le_x
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
// Đầu vào: họ địa chỉ ở edi, cổng trong khối điều khiển, IP tại +32.
    movl %edi, %edx
    leaq 48(%r12), %rdi
    xorl %eax, %eax
    movl $32, %ecx
    rep stosb
#ifdef __APPLE__
    cmpl $2, %edx
    jne 1f
    movb $16, 48(%r12)
    jmp 2f
1:  movb $28, 48(%r12)
2:  movb %dl, 49(%r12)
#else
    movw %dx, 48(%r12)
#endif
    movl 8(%r12), %eax
    rolw $8, %ax
    movw %ax, 50(%r12)
    cmpl $2, %edx
    jne 3f
    movl 32(%r12), %eax
    movl %eax, 52(%r12)
    ret
3:  movq 32(%r12), %rax
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
    call SOCK_LIB(socket)
    testl %eax, %eax
    js .Lo_cam_loi_tra_ve_x
    movl %eax, (%r12)
    call .Lo_cam_dat_thoi_gian_cho_x
    testl %eax, %eax
    js .Lo_cam_dong_loi_x
    movl (%r12), %edi
    leaq 48(%r12), %rsi
    movl $16, %edx
    call SOCK_LIB(bind)
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
    call SOCK_LIB(getsockname)
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
1:  movl 8(%r12), %eax
    testl %eax, %eax
    jle .Lo_cam_khong_hop_le_x
    cmpl $65535, %eax
    jg .Lo_cam_khong_hop_le_x
    cmpl $1, 4(%r12)
    ja .Lo_cam_khong_hop_le_x
    movl $2, %edi
    cmpl $16, 24(%r12)
    jne 2f
    movl $SOCK_INET6, %edi
2:  movl %edi, %ebx
    call .Lo_cam_dia_chi_x
    movl %ebx, %edi
    movl $1, %esi
    cmpl $1, 4(%r12)
    jne 3f
    movl $2, %esi
3:  xorl %edx, %edx
    call SOCK_LIB(socket)
    testl %eax, %eax
    js .Lo_cam_loi_tra_ve_x
    movl %eax, 0(%r12)
    call .Lo_cam_khong_sigpipe_x
    testl %eax, %eax
    js .Lo_cam_dong_loi_x
// Đặt SO_SNDTIMEO/SO_RCVTIMEO trước kết nối để giới hạn nhập xuất chặn.
    movl 12(%r12), %eax
    testl %eax, %eax
    jle .Lo_cam_khong_hop_le_mo_ta_x
    cmpl $300000, %eax
    jg .Lo_cam_khong_hop_le_mo_ta_x
    call .Lo_cam_dat_thoi_gian_cho_x
    testl %eax, %eax
    js .Lo_cam_dong_loi_x
// SO_SNDTIMEO không giới hạn connect. Chuyển sang không chặn rồi khôi phục cờ trước khi trả mô tả ổ
// cắm.
    movl (%r12), %edi
    movl $3, %esi                 // F_GETFL
    xorl %eax, %eax
    call SOCK_LIB(fcntl)
    testl %eax, %eax
    js .Lo_cam_errno_dong_x
    movl %eax, 104(%r12)
    orl $SOCK_NONBLOCK, %eax
    movl %eax, %edx
    movl (%r12), %edi
    movl $4, %esi                 // F_SETFL
    xorl %eax, %eax
    call SOCK_LIB(fcntl)
    testl %eax, %eax
    js .Lo_cam_errno_dong_x
    movl 0(%r12), %edi
    leaq 48(%r12), %rsi
    movl $16, %edx
    cmpl $16, 24(%r12)
    jne 4f
    movl $28, %edx
4:  call SOCK_LIB(connect)
    testl %eax, %eax
    jns .Lo_cam_da_noi_x
    call .Lo_cam_errno_x
    cmpl $-SOCK_INPROGRESS, %eax
    je .Lo_cam_ket_noi_cho_x
    cmpl $-4, %eax               // EINTR từ lần kết nối đầu
    jne .Lo_cam_ket_noi_khoi_phuc_loi_x
.Lo_cam_ket_noi_cho_x:
    movl (%r12), %eax
    movl %eax, 96(%r12)
    movw $4, 100(%r12)            // POLLOUT
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
    call SOCK_LIB(poll)
    testl %eax, %eax
    js .Lo_cam_ket_noi_tham_do_loi_x
    jz .Lo_cam_ket_noi_qua_han_x
    movl (%r12), %edi
    movl $SOCK_LEVEL, %esi
    movl $SOCK_SOERROR, %edx
    leaq 108(%r12), %rcx
    movl $4, 112(%r12)
    leaq 112(%r12), %r8
    call SOCK_LIB(getsockopt)
    testl %eax, %eax
    js .Lo_cam_ket_noi_tham_do_loi_x
    movl 108(%r12), %eax
    testl %eax, %eax
    jz .Lo_cam_da_noi_x
    movl %eax, 28(%r12)
    negl %eax
    jmp .Lo_cam_ket_noi_khoi_phuc_loi_x
.Lo_cam_ket_noi_qua_han_x:
    movl $SOCK_TIMEDOUT, 28(%r12)
    movl $-SOCK_TIMEDOUT, %eax
    jmp .Lo_cam_ket_noi_khoi_phuc_loi_x
.Lo_cam_ket_noi_tham_do_loi_x:
    call .Lo_cam_errno_x
    cmpl $-4, %eax                // EINTR
    je .Lo_cam_ket_noi_tham_do_lap_x
.Lo_cam_ket_noi_khoi_phuc_loi_x:
    movl %eax, %ebx
    movl (%r12), %edi
    movl $4, %esi
    movl 104(%r12), %edx
    xorl %eax, %eax
    call SOCK_LIB(fcntl)
    movl %ebx, %eax
    jmp .Lo_cam_dong_loi_x
.Lo_cam_da_noi_x:
    movl (%r12), %edi
    movl $4, %esi
    movl 104(%r12), %edx
    xorl %eax, %eax
    call SOCK_LIB(fcntl)
    testl %eax, %eax
    js .Lo_cam_errno_dong_x
    movl 0(%r12), %eax
    jmp .Lo_cam_tra_ve_x
.Lo_cam_lang_nghe_x:
    movl 8(%r12), %eax
    testl %eax, %eax
    jle .Lo_cam_khong_hop_le_x
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
    call SOCK_LIB(socket)
    testl %eax, %eax
    js .Lo_cam_loi_tra_ve_x
    movl %eax, (%r12)
    movl $1, 96(%r12)
    movl (%r12), %edi
    movl $SOCK_LEVEL, %esi
    movl $SOCK_REUSE, %edx           // SO_REUSEADDR
    leaq 96(%r12), %rcx
    movl $4, %r8d
    call SOCK_LIB(setsockopt)
    testl %eax, %eax
    js .Lo_cam_errno_dong_x
    movl (%r12), %edi
    leaq 48(%r12), %rsi
    movl $16, %edx
    call SOCK_LIB(bind)
    testl %eax, %eax
    js .Lo_cam_errno_dong_x
    movl (%r12), %edi
    movl 20(%r12), %esi
    call SOCK_LIB(listen)
    testl %eax, %eax
    js .Lo_cam_errno_dong_x
    movl (%r12), %eax
    jmp .Lo_cam_tra_ve_x
.Lo_cam_chap_nhan_x:
// Thời hạn poll giới hạn accept trên cả hai ABI POSIX.
    movl (%r12), %eax
    testl %eax, %eax
    js .Lo_cam_khong_hop_le_x
    movl %eax, 96(%r12)
    movw $1, 100(%r12)            // POLLIN
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
    call SOCK_LIB(poll)
    testl %eax, %eax
    js .Lo_cam_chap_nhan_tham_do_loi_x
    jz .Lo_cam_chap_nhan_qua_han_x
    movl (%r12), %edi
    xorl %esi, %esi
    xorl %edx, %edx
    call SOCK_LIB(accept)
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
    movl $SOCK_TIMEDOUT, 28(%r12)
    movl $-SOCK_TIMEDOUT, %eax
    jmp .Lo_cam_tra_ve_x
.Lo_cam_chap_nhan_tham_do_loi_x:
    call .Lo_cam_errno_x
    cmpl $-4, %eax               // EINTR
    je .Lo_cam_chap_nhan_tham_do_lap_x
    jmp .Lo_cam_tra_ve_x
.Lo_cam_nhap_xuat_x:
    movl 16(%r12), %esi
    movl 20(%r12), %edx
    testl %edx, %edx
    jle .Lo_cam_khong_hop_le_x
    cmpl $65536, %edx
    jg .Lo_cam_khong_hop_le_x
    cmpl $(68162560-65536), %esi
    ja .Lo_cam_khong_hop_le_x
    movl %esi, %eax
    addq %rax, %rdx
    cmpq $68162560, %rdx
    ja .Lo_cam_khong_hop_le_x
    movl %esi, %r13d             // libc có thể ghi đè thanh ghi tạm rsi
    call SOCK_SYM(vpp_native_memory_base)
    leaq (%rax,%r13), %rsi
    movl (%r12), %edi
    movl 20(%r12), %edx
    movl $SOCK_MSGFLAGS, %ecx
    cmpl $8, %ebx
    je .Lo_cam_nhan_goi_x
    cmpl $9, %ebx
    je .Lo_cam_gui_goi_x
    cmpl $3, %ebx
    jne 1f
    call SOCK_LIB(send)
    jmp 2f
1:  call SOCK_LIB(recv)
2:  testl %eax, %eax
    js .Lo_cam_loi_tra_ve_x
    jmp .Lo_cam_tra_ve_x
.Lo_cam_nhan_goi_x:
    xorl %ecx, %ecx
    movl $32, 112(%r12)
    leaq 48(%r12), %r8
    leaq 112(%r12), %r9
    call SOCK_LIB(recvfrom)
    testl %eax, %eax
    js .Lo_cam_loi_tra_ve_x
    jmp .Lo_cam_tra_ve_x
.Lo_cam_gui_goi_x:
    cmpl $16, 112(%r12)
    jne .Lo_cam_khong_hop_le_x
    leaq 48(%r12), %r8
    movl $16, %r9d
    call SOCK_LIB(sendto)
    testl %eax, %eax
    js .Lo_cam_loi_tra_ve_x
    jmp .Lo_cam_tra_ve_x
.Lo_cam_dong_x:
    movl (%r12), %edi
    testl %edi, %edi
    js .Lo_cam_khong_hop_le_x
    call SOCK_LIB(close)
    testl %eax, %eax
    js .Lo_cam_errno_x
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
// r12 giữ khối điều khiển còn hoạt động; trả 0 hoặc -errno. rsp%16==8 khi vào hàm phụ; căn chỉnh
// trước khi gọi libc.
    subq $8, %rsp
    movl 12(%r12), %eax
    xorl %edx, %edx
    movl $1000, %ecx
    divl %ecx
    movq %rax, 80(%r12)
    imull $1000, %edx, %edx
    movq %rdx, 88(%r12)
    movl (%r12), %edi
    movl $SOCK_LEVEL, %esi
    movl $SOCK_RCVTIMEO, %edx
    leaq 80(%r12), %rcx
    movl $16, %r8d
    call SOCK_LIB(setsockopt)
    testl %eax, %eax
    js .Lo_cam_thoi_gian_cho_loi_x
    movl (%r12), %edi
    movl $SOCK_LEVEL, %esi
    movl $SOCK_SNDTIMEO, %edx
    leaq 80(%r12), %rcx
    movl $16, %r8d
    call SOCK_LIB(setsockopt)
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
    subq $8, %rsp
#ifdef __APPLE__
    movl $1, 96(%r12)
    movl (%r12), %edi
    movl $SOCK_LEVEL, %esi
    movl $SOCK_NOSIGPIPE, %edx
    leaq 96(%r12), %rcx
    movl $4, %r8d
    call SOCK_LIB(setsockopt)
    testl %eax, %eax
    js .Lo_cam_khong_sigpipe_loi_x
#endif
    xorl %eax, %eax
    addq $8, %rsp
    ret
.Lo_cam_khong_sigpipe_loi_x:
    call .Lo_cam_errno_x
    addq $8, %rsp
    ret
// Hàm phụ dùng ô ngăn xếp của bên gọi: timespec@48..63, thời hạn tuyệt đối mili giây@80; căn chỉnh
// trước khi gọi libc.
.Lo_cam_han_khoi_dong_x:
    subq $8, %rsp
    movl $SOCK_CLOCK, %edi
    leaq 64(%rsp), %rsi
    call SOCK_LIB(clock_gettime)
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
    movl $SOCK_CLOCK, %edi
    leaq 64(%rsp), %rsi
    call SOCK_LIB(clock_gettime)
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
// call đẩy địa chỉ trở về nên các trường thời gian của hàm phụ lệch thêm 8 byte.
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
    call SOCK_LIB(close)
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
    movl $-22, %eax
    jmp .Lo_cam_dong_loi_x
.Lo_cam_errno_x:
// Bên gọi ngoài và các hàm lồng nhau đều đã căn chỉnh ngăn xếp.
    subq $8, %rsp
#ifdef __APPLE__
    call SOCK_LIB(__error)
#else
    call SOCK_LIB(__errno_location)
#endif
    addq $8, %rsp
    movl (%rax), %eax
    movl %eax, 28(%r12)
    negl %eax
    ret
.Lo_cam_khong_hop_le_x:
    movl $-22, %eax
.Lo_cam_tra_ve_x:
    addq $136, %rsp
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
