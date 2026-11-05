// Đọc định danh tệp và kiểm tra quyền truy cập.

.macro HAM_STAT ten, dich, duong
    .globl \ten
\ten:
    VAO_BO_NHO
    cmpl $(3376), %esi
    je 1f
    cmpl $(3584), %esi
    jne Lbo_nho_loi
1:
    leaq (%r11,%rsi), %rsi
    movq %rsi, -32(%rbp)
    .if \duong
        KIEM_TRA_DUONG %edi
        leaq (%r11,%rdi), %rdi
    .endif
    call \dich
#ifndef __APPLE__
// struct stat glibc x86-64 có dev +0, ino +8, mode +24. V++ đọc bố trí Darwin: dev +0, mode +4, ino
// +8.
    testl %eax, %eax
    jne 2f
    movq -32(%rbp), %r10
    movl 24(%r10), %r9d
    movl %r9d, 4(%r10)
2:
#endif
    TRA_BO_NHO
.endm
#ifdef __APPLE__
// _stat/_lstat/_fstat trên Darwin x86-64 dùng inode 32 bit kiểu cũ. Bố trí struct stat mà runtime.vi
// cần phải gọi ký hiệu $INODE64.
HAM_STAT PUBLIC(vpp_native_os_stat), _stat$INODE64, 1
HAM_STAT PUBLIC(vpp_native_os_lstat), _lstat$INODE64, 1
HAM_STAT PUBLIC(vpp_native_os_fstat), _fstat$INODE64, 0
#else
HAM_STAT PUBLIC(vpp_native_os_stat), Llinux_stat, 1
HAM_STAT PUBLIC(vpp_native_os_lstat), Llinux_lstat, 1
HAM_STAT PUBLIC(vpp_native_os_fstat), Llinux_stat_mo_ta, 0
#endif

.globl PUBLIC(vpp_native_os_access_exec)
PUBLIC(vpp_native_os_access_exec):
    VAO_BO_NHO
    KIEM_TRA_DUONG %edi
    leaq (%r11,%rdi), %rdi
    movl $1, %esi
#ifdef __APPLE__
    call LIB(access)
#else
    call Llinux_truy_cap
#endif
    TRA_BO_NHO
.globl PUBLIC(vpp_native_os_fchmod)
PUBLIC(vpp_native_os_fchmod):
#ifdef __APPLE__
    jmp LIB(fchmod)
#else
    movl $91, %eax              // SYS_fchmod(fd, mode)
    syscall
    jmp Llinux_goi_he_thong_ket_qua
#endif
.globl PUBLIC(vpp_native_os_unlink)
PUBLIC(vpp_native_os_unlink):
    VAO_BO_NHO
    KIEM_TRA_DUONG %edi
    leaq (%r11,%rdi), %rdi
#ifdef __APPLE__
    call LIB(unlink)
#else
    call Llinux_xoa_tep
#endif
    TRA_BO_NHO
.globl PUBLIC(vpp_native_os_mkdir)
PUBLIC(vpp_native_os_mkdir):
    VAO_BO_NHO
    KIEM_TRA_DUONG %edi
    leaq (%r11,%rdi), %rdi
    movl $448, %esi           // Quyền 0700
#ifdef __APPLE__
    call LIB(mkdir)
#else
    call Llinux_tao_thu_muc
#endif
    TRA_BO_NHO

