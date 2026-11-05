// Đọc định danh tệp và kiểm tra quyền truy cập.

// Trả định danh stat 16 byte theo bố trí Darwin trong ô siêu dữ liệu V++: dev@0, S_IFMT mode@4, ID
// tệp 64 bit@8. BY_HANDLE_FILE_INFORMATION dài 52 byte: số ổ đĩa@28, ID tệp cao/thấp@44/+48. Mã tài
// nguyên Windows được mượn. rcx: mã tài nguyên; edx: ô đã kiểm tra; r8d: lstat, không theo điểm
// chuyển hướng.
Ltep_dinh_danh:
    pushq %rbp
    movq %rsp, %rbp
    subq $192, %rsp
    movq %rcx, -8(%rbp)
    movl %edx, -12(%rbp)
    movl %r8d, -16(%rbp)
    leaq -128(%rbp), %rdx
    call GetFileInformationByHandle
    testl %eax, %eax
    jz Ltep_dinh_danh_loi
    call vpp_native_memory_base
    testq %rax, %rax
    jz Ltep_dinh_danh_loi
    movl -12(%rbp), %ecx
    leaq (%rax,%rcx), %rdx
    movl -100(%rbp), %ecx       // Số định danh ổ đĩa
    movl %ecx, (%rdx)
    movl $0x8000, %ecx         // S_IFREG, dùng bởi trình liên kết V++
    movl -128(%rbp), %r8d      // dwFileAttributes
    testl $0x10, %r8d          // FILE_ATTRIBUTE_DIRECTORY
    jz 1f
    movl $0x4000, %ecx         // S_IFDIR
1:  cmpl $0, -16(%rbp)
    je 2f
    testl $0x400, %r8d         // FILE_ATTRIBUTE_REPARSE_POINT
    jz 2f
    movl $0xa000, %ecx         // S_IFLNK / điểm chuyển hướng
2:  movw %cx, 4(%rdx)
    movw $0, 6(%rdx)
    movl -80(%rbp), %ecx       // nFileIndexLow
    movl %ecx, 8(%rdx)
    movl -84(%rbp), %ecx       // nFileIndexHigh
    movl %ecx, 12(%rdx)
    xorl %eax, %eax
    leave
    ret
Ltep_dinh_danh_loi:
    movl $-1, %eax
    leave
    ret

.globl vpp_native_os_stat
vpp_native_os_stat:
    xorl %r8d, %r8d
    jmp Lduong_tep_dinh_danh
.globl vpp_native_os_lstat
vpp_native_os_lstat:
    movl $1, %r8d
Lduong_tep_dinh_danh:
    VAO_BO_NHO
    KIEM_TRA_DUONG %ecx
    cmpl $3376, %edx
    je 1f
    cmpl $3584, %edx
    jne Lbo_nho_loi
1:  movl %edx, -24(%rbp)
    movl %r8d, -28(%rbp)
    leaq (%r11,%rcx), %rcx
    subq $8256, %rsp
    leaq 64(%rsp), %rdx
    call Lduong_utf8_sang_utf16
    testl %eax, %eax
    je Lduong_tep_chuyen_loi
    leaq 64(%rsp), %rcx
    movl $0x80, %edx          // FILE_READ_ATTRIBUTES
    movl $7, %r8d            // FILE_SHARE_READ|WRITE|DELETE
    xorl %r9d, %r9d          // Không truyền thuộc tính bảo mật
    movl $3, 32(%rsp)        // OPEN_EXISTING
    movl $0x02000000, %eax  // FILE_FLAG_BACKUP_SEMANTICS
    cmpl $0, -28(%rbp)
    je 2f
    orl $0x00200000, %eax   // FILE_FLAG_OPEN_REPARSE_POINT
2:  movl %eax, 40(%rsp)
    movq $0, 48(%rsp)
    call CreateFileW
    cmpq $-1, %rax
    je Lduong_tep_thieu
    movq %rax, -32(%rbp)
    movq %rax, %rcx
    movl -24(%rbp), %edx
    movl -28(%rbp), %r8d
    call Ltep_dinh_danh
    movl %eax, -36(%rbp)
    movq -32(%rbp), %rcx
    call CloseHandle
    movl -36(%rbp), %eax
    addq $8256, %rsp
    TRA_BO_NHO
Lduong_tep_thieu:
// GetLastError Win32 và errno UCRT là hai giá trị riêng theo luồng. Trình liên kết V++ chỉ coi
// errno=ENOENT là chưa có tệp đích.
    call GetLastError
    movl %eax, -36(%rbp)
    call _errno
    movl $13, (%rax)         // EACCES cho các lỗi khác
    cmpl $2, -36(%rbp)       // ERROR_FILE_NOT_FOUND
    je 3f
    cmpl $3, -36(%rbp)       // ERROR_PATH_NOT_FOUND
    jne 4f
3:  movl $2, (%rax)         // ENOENT
4:  movl $-1, %eax
    addq $8256, %rsp
    TRA_BO_NHO
Lduong_tep_chuyen_loi:
    addq $8256, %rsp
    jmp Lbo_nho_loi

.globl vpp_native_os_fstat
vpp_native_os_fstat:
    VAO_BO_NHO
    cmpl $3376, %edx
    je 1f
    cmpl $3584, %edx
    jne Lbo_nho_loi
1:  movl %edx, -24(%rbp)
    call _get_osfhandle
    cmpq $-1, %rax
    je Lbo_nho_loi
    movq %rax, %rcx
    movl -24(%rbp), %edx
    xorl %r8d, %r8d
    call Ltep_dinh_danh
    TRA_BO_NHO

