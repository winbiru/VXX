// Đọc/ghi, mở/đóng và thao tác tệp.

.globl PUBLIC(vpp_native_os_open)
PUBLIC(vpp_native_os_open):
    VAO_BO_NHO
    KIEM_TRA_DUONG %edi
    leaq (%r11,%rdi), %rdi
#ifndef __APPLE__
// runtime.vi truyền cờ Darwin O_WRONLY|O_CREAT|O_TRUNC = 0x601; Linux cần giá trị 0x241.
    cmpl $1537, %esi
    jne 1f
    movl $577, %esi
    jmp 2f
1:
    cmpl $521, %esi             // Darwin O_WRONLY|O_CREAT|O_APPEND
    jne 2f
    movl $1089, %esi            // Linux O_WRONLY|O_CREAT|O_APPEND
2:
#endif
    movl $438, %edx             // 0666, chịu ảnh hưởng umask của tiến trình
#ifdef __APPLE__
    movq %rdx, (%rsp)           // Đối số biến thiên Darwin x86-64 truyền qua ngăn xếp
#endif
#ifdef __APPLE__
    xorl %eax, %eax
    call LIB(open)
#else
    call Llinux_mo
#endif
    TRA_BO_NHO
.globl PUBLIC(vpp_native_os_close)
PUBLIC(vpp_native_os_close):
#ifdef __APPLE__
    jmp LIB(close)
#else
    jmp Llinux_dong
#endif
// Signed i32 position, checked against the full 64-bit lseek result.
.globl PUBLIC(vpp_native_os_seek)
PUBLIC(vpp_native_os_seek):
    movslq %esi, %rsi
    subq $8, %rsp
#ifdef __APPLE__
    call LIB(lseek)
#else
    movl $8, %eax
    syscall
    call Llinux_goi_he_thong_ket_qua
#endif
    testq %rax, %rax
    js 1f
    cmpq $2147483647, %rax
    jle 1f
#ifdef __APPLE__
    call LIB(__error)
    movl $84, (%rax)
#else
    movl $75, vpp_linux_host_errno(%rip)
#endif
    movl $-1, %eax
1:  addq $8, %rsp
    ret
.globl PUBLIC(vpp_native_os_errno)
PUBLIC(vpp_native_os_errno):
#ifdef __APPLE__
    subq $8, %rsp
    call LIB(__error)
    movl (%rax), %eax
    addq $8, %rsp
#else
    movl vpp_linux_host_errno(%rip), %eax
#endif
    ret

.globl PUBLIC(vpp_native_os_mkstemp)
PUBLIC(vpp_native_os_mkstemp):
    VAO_BO_NHO
    KIEM_TRA_DUONG %edi
    leaq (%r11,%rdi), %rdi
#ifdef __APPLE__
    call LIB(mkstemp)
#else
// Mẫu tên phải kết thúc bằng NUL trong vùng nhớ và có đuôi XXXXXX. Sinh đuôi ngẫu nhiên rồi mở bằng
// openat(O_CREAT|O_EXCL).
    movq %rdi, -32(%rbp)
    xorl %ecx, %ecx
.Lmay_tam_do_dai:
    cmpl $VPP_PATH_MAX, %ecx
    jae Lbo_nho_loi
    cmpb $0, (%rdi,%rcx)
    je .Lmay_tam_cuoi
    incl %ecx
    jmp .Lmay_tam_do_dai
.Lmay_tam_cuoi:
    cmpl $6, %ecx
    jb Lbo_nho_loi
    leaq -6(%rdi,%rcx), %r8
    movq %r8, -24(%rbp)
    movl $6, %ecx
.Lmay_tam_kiem_tra:
    cmpb $'X', (%r8)
    jne Lbo_nho_loi
    incq %r8
    decl %ecx
    jne .Lmay_tam_kiem_tra
    movl $64, -16(%rbp)        // Chỉ thử lại khi lỗi EEXIST
.Lmay_tam_thu_lai:
    leaq -40(%rbp), %rdi
    movl $8, %esi
    xorl %edx, %edx
    call vpp_sys_getrandom
    cmpq $-4, %rax             // EINTR
    je .Lmay_tam_thu_lai
    cmpq $8, %rax
    je .Lmay_tam_ngau_nhien
    cmpq $-4095, %rax
    jae .Lmay_tam_ket_qua
    movq $-5, %rax             // getrandom trả thiếu byte: EIO
    jmp .Lmay_tam_ket_qua
.Lmay_tam_ngau_nhien:
    movq -40(%rbp), %rax
    movq -24(%rbp), %r8
    leaq .Lmay_tam_bang_chu(%rip), %r9
    movl $62, %r10d
    movl $6, %ecx
.Lmay_tam_lap_day:
    xorl %edx, %edx
    divq %r10
    movb (%r9,%rdx), %dl
    movb %dl, (%r8)
    incq %r8
    decl %ecx
    jne .Lmay_tam_lap_day
    movq $-100, %rdi           // AT_FDCWD
    movq -32(%rbp), %rsi
    movl $194, %edx            // O_RDWR | O_CREAT | O_EXCL
    movl $384, %ecx            // 0600
    call vpp_sys_openat
    cmpq $-17, %rax            // EEXIST
    jne .Lmay_tam_ket_qua
    decl -16(%rbp)
    jne .Lmay_tam_thu_lai
.Lmay_tam_ket_qua:
    call Llinux_goi_he_thong_ket_qua
#endif
    TRA_BO_NHO
.globl PUBLIC(vpp_native_os_rename)
PUBLIC(vpp_native_os_rename):
    VAO_BO_NHO
    KIEM_TRA_DUONG %edi
    KIEM_TRA_DUONG %esi
    leaq (%r11,%rdi), %rdi
    leaq (%r11,%rsi), %rsi
#ifdef __APPLE__
    call LIB(rename)
#else
    call Llinux_doi_ten
#endif
    TRA_BO_NHO

