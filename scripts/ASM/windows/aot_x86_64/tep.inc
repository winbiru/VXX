// Đọc/ghi, mở/đóng và thao tác tệp.

.globl vpp_native_os_open
vpp_native_os_open:
    VAO_BO_NHO
    KIEM_TRA_DUONG %ecx
    leaq (%r11,%rcx), %rcx
    movl %edx, -36(%rbp)
// Dành 64 byte dưới đường UTF-16 cho vùng đối số dự phòng Win64; _wopen có thể ghi đối số vào [rsp,
// rsp+32).
    subq $8256, %rsp
    leaq 64(%rsp), %rdx
    call Lduong_utf8_sang_utf16
    testl %eax, %eax
    je Lmo_loi
    leaq 64(%rsp), %rcx
    movl -36(%rbp), %edx
    cmpl $0, %edx           // Chế độ đọc của runtime.vi
    je 1f
    cmpl $4, %edx           // Thăm dò nguồn bộ thực thi bằng đọc không chặn
    je 1f
    cmpl $1537, %edx        // Darwin O_WRONLY|O_CREAT|O_TRUNC
    jne 3f
    movl $0x8301, %edx      // _O_BINARY|_O_CREAT|_O_TRUNC|_O_WRONLY
    jmp 2f
3:
    cmpl $521, %edx         // Darwin O_WRONLY|O_CREAT|O_APPEND
    jne 4f
    movl $0x8109, %edx      // _O_BINARY|_O_CREAT|_O_APPEND|_O_WRONLY
    jmp 2f
4:  cmpl $514, %edx
    jne Lmo_loi
    movl $0x8102, %edx      // _O_BINARY|_O_CREAT|_O_RDWR
    jmp 2f
1:  movl $0x8000, %edx      // _O_RDONLY|_O_BINARY
2:  movl $0x180, %r8d       // _S_IREAD|_S_IWRITE
    call _wopen
    addq $8256, %rsp
    TRA_BO_NHO
Lmo_loi:
    addq $8256, %rsp
    jmp Lbo_nho_loi
.globl vpp_native_os_close
vpp_native_os_close:
    jmp _close
// _lseeki64 returns a 64-bit offset; check before returning i32.
.globl vpp_native_os_seek
vpp_native_os_seek:
    movslq %edx, %rdx
    subq $40, %rsp
    call _lseeki64
    testq %rax, %rax
    js 1f
    cmpq $2147483647, %rax
    jle 1f
    call _errno
    movl $132, (%rax) // UCRT EOVERFLOW
    movl $-1, %eax
1:  addq $40, %rsp
    ret
.globl vpp_native_os_errno
vpp_native_os_errno:
    subq $40, %rsp
    call _errno
    movl (%rax), %eax
    addq $40, %rsp
    ret
.globl vpp_native_os_rename
vpp_native_os_rename:
    VAO_BO_NHO
    KIEM_TRA_DUONG %ecx
    KIEM_TRA_DUONG %edx
    movl %edx, -24(%rbp)
    leaq (%r11,%rcx), %rcx
// Hai đường 4096 WCHAR cùng vùng đối số dự phòng Win64.
    subq $16448, %rsp
    leaq 64(%rsp), %rdx
    call Lduong_utf8_sang_utf16
    testl %eax, %eax
    je Ldoi_ten_chuyen_loi
    call vpp_native_memory_base
    movl -24(%rbp), %edx
    leaq (%rax,%rdx), %rcx
    leaq 8256(%rsp), %rdx
    call Lduong_utf8_sang_utf16
    testl %eax, %eax
    je Ldoi_ten_chuyen_loi
    leaq 64(%rsp), %rcx
    leaq 8256(%rsp), %rdx
// UCRT rename từ chối đích đã tồn tại. Dùng thao tác thay thế nguyên tử trên cùng ổ đĩa của Windows,
// giữ quy ước kết quả V++ 0/-1.
    movl $9, %r8d          // MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
    call MoveFileExW
    addq $16448, %rsp
    testl %eax, %eax
    jz Lbo_nho_loi
    xorl %eax, %eax
    TRA_BO_NHO
Ldoi_ten_chuyen_loi:
    addq $16448, %rsp
    jmp Lbo_nho_loi
.globl vpp_native_os_unlink
vpp_native_os_unlink:
    VAO_BO_NHO
    KIEM_TRA_DUONG %ecx
    leaq (%r11,%rcx), %rcx
    subq $8256, %rsp
    leaq 64(%rsp), %rdx
    call Lduong_utf8_sang_utf16
    testl %eax, %eax
    je Lxoa_tep_chuyen_loi
    leaq 64(%rsp), %rcx
    call _wunlink
#ifndef VPP_WINDOWS_I32_BOOTSTRAP
    testl %eax, %eax
    jz Lxoa_tep_xong
    call _errno
    cmpl $13, (%rax)          // EACCES: UCRT rejects read-only files
    jne Lxoa_tep_that_bai
    // Open the entry itself, never its reparse target. Delete by handle
    // without clearing READONLY (which could affect other hardlinks).
    leaq 64(%rsp), %rcx
    movl $0x10080, %edx       // DELETE | FILE_READ_ATTRIBUTES
    movl $7, %r8d             // FILE_SHARE_READ | WRITE | DELETE
    xorl %r9d, %r9d
    movl $3, 32(%rsp)         // OPEN_EXISTING
    movl $0x00200000, 40(%rsp) // FILE_FLAG_OPEN_REPARSE_POINT
    movq $0, 48(%rsp)
    call CreateFileW
    cmpq $-1, %rax
    je Lxoa_tep_loi_quyen
    movq %rax, -32(%rbp)
    movl $0, -36(%rbp)
    movq %rax, %rcx
    leaq -88(%rbp), %rdx      // BY_HANDLE_FILE_INFORMATION (52 bytes)
    call GetFileInformationByHandle
    testl %eax, %eax
    jz Lxoa_tep_dong
    movl -88(%rbp), %eax
    testl $0x410, %eax        // DIRECTORY | REPARSE_POINT
    jnz Lxoa_tep_dong
    testl $1, %eax            // FILE_ATTRIBUTE_READONLY
    jz Lxoa_tep_dong
    movl $0x13, -40(%rbp)     // DELETE | POSIX_SEMANTICS | IGNORE_READONLY_ATTRIBUTE
    movq -32(%rbp), %rcx
    movl $21, %edx            // FileDispositionInfoEx
    leaq -40(%rbp), %r8
    movl $4, %r9d
    call SetFileInformationByHandle
    movl %eax, -36(%rbp)
Lxoa_tep_dong:
    movq -32(%rbp), %rcx
    call CloseHandle
    cmpl $0, -36(%rbp)
    je Lxoa_tep_loi_quyen
    xorl %eax, %eax
    jmp Lxoa_tep_xong
Lxoa_tep_loi_quyen:
    call _errno
    movl $13, (%rax)          // Preserve the original UCRT failure
Lxoa_tep_that_bai:
    movl $-1, %eax
Lxoa_tep_xong:
#endif
    addq $8256, %rsp
    TRA_BO_NHO
Lxoa_tep_chuyen_loi:
    addq $8256, %rsp
    jmp Lbo_nho_loi
.globl vpp_native_os_mkdir
vpp_native_os_mkdir:
    VAO_BO_NHO
    KIEM_TRA_DUONG %ecx
    leaq (%r11,%rcx), %rcx
    subq $8256, %rsp
    leaq 64(%rsp), %rdx
    call Lduong_utf8_sang_utf16
    testl %eax, %eax
    je Ltao_thu_muc_chuyen_loi
    leaq 64(%rsp), %rcx
    call _wmkdir
    addq $8256, %rsp
    TRA_BO_NHO
Ltao_thu_muc_chuyen_loi:
    addq $8256, %rsp
    jmp Lbo_nho_loi
.globl vpp_native_os_access_exec
vpp_native_os_access_exec:
    VAO_BO_NHO
    KIEM_TRA_DUONG %ecx
    leaq (%r11,%rcx), %rcx
    subq $8256, %rsp
    leaq 64(%rsp), %rdx
    call Lduong_utf8_sang_utf16
    testl %eax, %eax
    je Ltruy_cap_chuyen_loi
    leaq 64(%rsp), %rcx
    xorl %edx, %edx         // _access Windows chỉ hỗ trợ kiểm tra tồn tại/đọc/ghi
    call _waccess
    addq $8256, %rsp
    TRA_BO_NHO
Ltruy_cap_chuyen_loi:
    addq $8256, %rsp
    jmp Lbo_nho_loi

// Trình liên kết V++ cấp đường có thể ghi, kết thúc NUL và có đuôi XXXXXX. _wmktemp_s chọn tên,
// _O_EXCL ngăn ghi đè khi tranh chấp; bên gọi sở hữu mô tả và tên mới.
.globl vpp_native_os_mkstemp
vpp_native_os_mkstemp:
    VAO_BO_NHO
    KIEM_TRA_DUONG %ecx
    leaq (%r11,%rcx), %r9
    movq %r9, -32(%rbp)
    xorl %eax, %eax
Lmkstemp_do_dai:
    cmpl $4096, %eax
    jae Lbo_nho_loi
    cmpb $0, (%r9,%rax)
    je Lmkstemp_duoi
    incl %eax
    jmp Lmkstemp_do_dai
Lmkstemp_duoi:
    cmpl $6, %eax
    jb Lbo_nho_loi
    leaq -6(%r9,%rax), %r10
    xorl %eax, %eax
Lmkstemp_kiem_tra_x:
    cmpb $'X', (%r10,%rax)
    jne Lbo_nho_loi
    incl %eax
    cmpl $6, %eax
    jb Lmkstemp_kiem_tra_x
// Chọn và mở tên tệp tạm bằng UTF-16 để giữ đường tiếng Việt. Chỉ đổi tên đã chọn về đệm UTF-8 của
// bên gọi sau khi tạo độc quyền thành công.
    movq -32(%rbp), %rcx
    subq $8256, %rsp
    leaq 64(%rsp), %rdx
    call Lduong_utf8_sang_utf16
    testl %eax, %eax
    je Lmkstemp_chuyen_loi
    leaq 64(%rsp), %rcx
    movl $4096, %edx
    call _wmktemp_s
    testl %eax, %eax
    jne Lmkstemp_chuyen_loi
    leaq 64(%rsp), %rcx
    movl $0x8502, %edx   // _O_BINARY|_O_CREAT|_O_EXCL|_O_RDWR
    movl $0x180, %r8d    // _S_IREAD|_S_IWRITE
    call _wopen
    cmpl $-1, %eax
    je Lmkstemp_chuyen_loi
    movl %eax, -36(%rbp)
    movl $65001, %ecx
    xorl %edx, %edx
    leaq 64(%rsp), %r8
    movl $-1, %r9d
    movq -32(%rbp), %rax
    movq %rax, 32(%rsp)
    movl $4096, 40(%rsp)
    movq $0, 48(%rsp)
    movq $0, 56(%rsp)
    call WideCharToMultiByte
    testl %eax, %eax
    je Lmkstemp_da_tao_loi
    movl -36(%rbp), %eax
    addq $8256, %rsp
    TRA_BO_NHO
Lmkstemp_da_tao_loi:
    movl -36(%rbp), %ecx
    call _close
    leaq 64(%rsp), %rcx
    call _wunlink
Lmkstemp_chuyen_loi:
    addq $8256, %rsp
    jmp Lbo_nho_loi

