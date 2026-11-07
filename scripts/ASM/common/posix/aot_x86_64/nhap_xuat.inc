// Hàm bọc nhập xuất và chuyển kết quả lời gọi hệ thống.

.macro HAM_NHAP_XUAT ten, dich
    .globl \ten
\ten:
    VAO_BO_NHO
    KIEM_TRA_LECH %esi, 0
    cmpl $(0), %edx
    jl Lbo_nho_loi
    movl %esi, %eax
    addq %rdx, %rax
    cmpq $(VPP_ARENA_SIZE), %rax
    ja Lbo_nho_loi
    leaq (%r11,%rsi), %rsi
    movslq %edx, %rdx
    call \dich
    TRA_BO_NHO
.endm
#ifndef __APPLE__
// Đổi kết quả syscall Linux sang ABI của bộ biên dịch: thành công >= 0; lỗi trả -1 và lưu errno. Bộ
// biên dịch hiện chỉ chạy một luồng.
Llinux_goi_he_thong_ket_qua:
    cmpq $-4095, %rax
    jb 1f
    negl %eax
    movl %eax, vpp_linux_host_errno(%rip)
    movq $-1, %rax
1:  ret

Llinux_doc:
    xorl %eax, %eax              // SYS_read
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_ghi:
    movl $1, %eax                // SYS_write
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_dong:
    movl $3, %eax                // SYS_close
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_mo:
    movl %edx, %r10d             // Quyền truy cập
    movl %esi, %edx              // Cờ
    movq %rdi, %rsi              // Đường dẫn
    movl $-100, %edi             // AT_FDCWD
    movl $257, %eax              // SYS_openat
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_stat:
    movq %rsi, %rdx             // Đệm stat
    movq %rdi, %rsi             // Đường dẫn
    movq $-100, %rdi           // AT_FDCWD
    xorl %r10d, %r10d          // Theo liên kết tượng trưng
    movl $262, %eax            // SYS_newfstatat
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_lstat:
    movq %rsi, %rdx
    movq %rdi, %rsi
    movq $-100, %rdi
    movl $256, %r10d           // AT_SYMLINK_NOFOLLOW
    movl $262, %eax
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_stat_mo_ta:
    movl $5, %eax              // SYS_fstat(fd, buf)
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_truy_cap:
    movl %esi, %edx            // Quyền truy cập
    movq %rdi, %rsi            // Đường dẫn
    movq $-100, %rdi
    xorl %r10d, %r10d          // Cờ
    movl $269, %eax            // SYS_faccessat
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_xoa_tep:
    movq %rdi, %rsi
    movq $-100, %rdi
    xorl %edx, %edx            // Xóa tệp
    movl $263, %eax            // SYS_unlinkat
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_tao_thu_muc:
    movl %esi, %edx            // Quyền truy cập
    movq %rdi, %rsi
    movq $-100, %rdi
    movl $258, %eax            // SYS_mkdirat
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_doi_ten:
    movq %rsi, %r10            // Đường đích
    movq $-100, %rdx           // Mô tả thư mục đích
    movq %rdi, %rsi            // Đường nguồn
    movq $-100, %rdi           // Mô tả thư mục nguồn
    movl $264, %eax            // SYS_renameat
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_doc_lien_ket:
    movq %rdx, %r10            // Độ dài đầu ra
    movq %rsi, %rdx            // Đệm đầu ra
    movq %rdi, %rsi            // Đường liên kết tượng trưng
    movq $-100, %rdi
    movl $267, %eax            // SYS_readlinkat
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_lay_thu_muc:
    movl $79, %eax             // SYS_getcwd
    syscall
    jmp Llinux_goi_he_thong_ket_qua
Llinux_doi_pid:
    xorl %r10d, %r10d          // Không lấy struct rusage
    movl $61, %eax             // SYS_wait4
    syscall
    jmp Llinux_goi_he_thong_ket_qua

HAM_NHAP_XUAT PUBLIC(vpp_native_os_read), Llinux_doc
HAM_NHAP_XUAT PUBLIC(vpp_native_os_write), Llinux_ghi
#else
HAM_NHAP_XUAT PUBLIC(vpp_native_os_read), LIB(read)
HAM_NHAP_XUAT PUBLIC(vpp_native_os_write), LIB(write)
#endif

