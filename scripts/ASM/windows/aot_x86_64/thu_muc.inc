// Directory handles remain native 64-bit pointers in the 16-byte control.
// Records returned to V++ use the same checked framing as Linux getdents64.
.text
.globl vpp_native_os_directory
vpp_native_os_directory:
    WIN_VAO
    cmpl $9, %r12d
    je .Lwin_pwrite
    cmpl $10, %r12d
    je .Lwin_fsync
    cmpl $11, %r12d
    je .Lwin_dirsync
    cmpl $12, %r12d
    je .Lwin_lock
    cmpl $13, %r12d
    je .Lwin_hardlink
    cmpl $8, %r12d
    ja .Lwin_khong_hop_le
    cmpl $6, %r12d
    jne 1f
    movl $2, %eax
    cmpl $1, %r13d
    jne 2f
    movl $120000000, %eax
2:  WIN_RA
1:  testl %r13d, %r13d
    jz .Lwin_khong_hop_le
    cmpl $(VPP_ARENA_SIZE-8192), %r13d
    ja .Lwin_khong_hop_le
    cmpl $(VPP_ARENA_SIZE-8192), %r14d
    ja .Lwin_khong_hop_le
    leaq (%rbx,%r13), %rsi
    leaq (%rbx,%r14), %rdi
    testl %r12d, %r12d
    jz .Lwin_thu_muc_mo
    cmpl $1, %r12d
    je .Lwin_thu_muc_doc
    cmpl $2, %r12d
    je .Lwin_thu_muc_dong
    // Path conversion has heap storage, so long Unicode paths never jump
    // over a Win64 stack guard page.
    movl $1, %ecx
    movl $16384, %edx
    call calloc
    testq %rax, %rax
    jz .Lwin_khong_hop_le
    movq %rax, %r15
    movq %rsi, %rcx
    movq %r15, %rdx
    call Lduong_utf8_sang_utf16
    testl %eax, %eax
    jz .Lwin_thu_muc_duong_loi
    cmpl $7, %r12d
    je .Lwin_thu_muc_day_du
    cmpl $8, %r12d
    je .Lwin_thu_muc_day_du
    cmpl $5, %r12d
    je .Lwin_thu_muc_lien_ket
    movq %r15, %rcx
    cmpl $3, %r12d
    je .Lwin_thu_muc_xoa
    xorl %edx, %edx
    call CreateDirectoryW
    jmp .Lwin_thu_muc_doi_ket_qua
.Lwin_thu_muc_xoa:
    call RemoveDirectoryW
.Lwin_thu_muc_doi_ket_qua:
    testl %eax, %eax
    jz .Lwin_thu_muc_api_loi
    xorl %r12d, %r12d
    jmp .Lwin_thu_muc_giai_phong
.Lwin_thu_muc_api_loi:
    call .Lwin_loi
    movl %eax, %r12d
    jmp .Lwin_thu_muc_giai_phong
.Lwin_thu_muc_lien_ket:
    // GetFinalPathNameByHandleW resolves symlinks for canonicalization;
    // a raw POSIX readlink operation has no matching Windows contract.
    movl $-22, %r12d
    jmp .Lwin_thu_muc_giai_phong
.Lwin_thu_muc_day_du:
    movq %r15, %rcx
    movl $4096, %edx
    leaq 8192(%r15), %r8
    xorl %r9d, %r9d
    call GetFullPathNameW
    testl %eax, %eax
    jz .Lwin_thu_muc_api_loi
    cmpl $4096, %eax
    jae .Lwin_thu_muc_duong_loi
    leaq 8192(%r15), %rcx
    movq %rdi, %rdx
    call Lduong_utf16_sang_utf8
    testl %eax, %eax
    jz .Lwin_thu_muc_duong_loi
    leal -1(%rax), %r12d
    // Internal paths use forward slashes on all platforms.
    xorl %eax, %eax
1:  cmpl %r12d, %eax
    jae .Lwin_thu_muc_giai_phong
    cmpb $92, (%rdi,%rax)
    jne 2f
    movb $47, (%rdi,%rax)
2:  incl %eax
    jmp 1b
.Lwin_thu_muc_duong_loi:
    movl $-36, %r12d
.Lwin_thu_muc_giai_phong:
    movq %r15, %rcx
    call free
    movl %r12d, %eax
    WIN_RA

.Lwin_thu_muc_mo:
    movl $1, %ecx
    movl $9000, %edx
    call calloc
    testq %rax, %rax
    jz .Lwin_khong_hop_le
    movq %rax, %r15
    movq %rsi, %rcx
    leaq 608(%r15), %rdx
    call Lduong_utf8_sang_utf16
    testl %eax, %eax
    jz .Lwin_thu_muc_duong_loi
    cmpl $4093, %eax
    ja .Lwin_thu_muc_duong_loi
    leaq 606(%r15,%rax,2), %rcx
    movw $92, (%rcx)
    movw $42, 2(%rcx)
    movw $0, 4(%rcx)
    leaq 608(%r15), %rcx
    leaq 8(%r15), %rdx
    call FindFirstFileW
    cmpq $-1, %rax
    je .Lwin_thu_muc_api_loi
    movq %rax, (%rdi)
    movq %r15, 8(%rdi)
    movl $1, (%r15)
    xorl %eax, %eax
    WIN_RA
.Lwin_thu_muc_doc:
    movq 8(%rsi), %r15
    testq %r15, %r15
    jz .Lwin_khong_hop_le
    cmpl $0, (%r15)
    jne .Lwin_thu_muc_ban_ghi
    movq (%rsi), %rcx
    leaq 8(%r15), %rdx
    call FindNextFileW
    testl %eax, %eax
    jnz .Lwin_thu_muc_ban_ghi
    call GetLastError
    cmpl $18, %eax
    jne 1f
    xorl %eax, %eax
    WIN_RA
1:  call .Lwin_loi
    WIN_RA
.Lwin_thu_muc_ban_ghi:
    movl $0, (%r15)
    leaq 52(%r15), %rcx
    leaq 19(%rdi), %rdx
    call Lduong_utf16_sang_utf8
    testl %eax, %eax
    jz .Lwin_khong_hop_le
    addl $19, %eax
    movq $0, (%rdi)
    movq $0, 8(%rdi)
    movw %ax, 16(%rdi)
    movb $8, 18(%rdi)
    testl $0x10, 8(%r15)
    jz 1f
    movb $4, 18(%rdi)
1:  testl $0x400, 8(%r15)
    jz 2f
    movb $10, 18(%rdi)
2:  WIN_RA
.Lwin_thu_muc_dong:
    movq 8(%rsi), %r15
    testq %r15, %r15
    jz .Lwin_thu_muc_da_dong
    movq $0, 8(%rsi)
    movq (%rsi), %rcx
    movq $-1, (%rsi)
    call FindClose
    movl %eax, %r12d
    movq %r15, %rcx
    call free
    testl %r12d, %r12d
    jz .Lwin_khong_hop_le
    xorl %eax, %eax
    WIN_RA

.Lwin_thu_muc_da_dong:
    movl $-9, %eax
    WIN_RA

// Open an independent file handle so positional writes cannot move the
// CRT descriptor's shared file pointer, even for concurrent callers.
.Lwin_pwrite:
    cmpl $(VPP_ARENA_SIZE-16), %r13d
    ja .Lwin_khong_hop_le
    leaq (%rbx,%r13), %r14
    movl 4(%r14), %r12d
    testl %r12d, %r12d
    jz .Lwin_khong_hop_le
    cmpl $65536, %r12d
    ja .Lwin_khong_hop_le
    movl $(VPP_ARENA_SIZE-16), %eax
    subl %r13d, %eax
    cmpl %eax, %r12d
    ja .Lwin_khong_hop_le
    cmpq $0, 8(%r14)
    jl .Lwin_khong_hop_le
    movl (%r14), %ecx
    testl %ecx, %ecx
    js .Lwin_durable_bad_fd
    call _get_osfhandle
    cmpq $-2, %rax
    je .Lwin_durable_bad_fd
    cmpq $-1, %rax
    je .Lwin_durable_bad_fd
    movq %rax, %rcx
    movl $0x40000000, %edx      // GENERIC_WRITE
    movl $7, %r8d              // FILE_SHARE_READ | WRITE | DELETE
    xorl %r9d, %r9d
    call ReOpenFile
    cmpq $-1, %rax
    je .Lwin_durable_api_error
    movq %rax, %r15
    movq %r15, %rcx
    movq 8(%r14), %rdx
    xorl %r8d, %r8d
    xorl %r9d, %r9d            // FILE_BEGIN
    call SetFilePointerEx
    testl %eax, %eax
    jz .Lwin_pwrite_error
    movq %r15, %rcx
    leaq 16(%r14), %rdx
    movl %r12d, %r8d
    leaq -64(%rbp), %r9
    movl $0, -64(%rbp)
    movq $0, 32(%rsp)          // synchronous WriteFile
    call WriteFile
    testl %eax, %eax
    jz .Lwin_pwrite_error
    movl -64(%rbp), %r12d
    jmp .Lwin_pwrite_close
.Lwin_pwrite_error:
    call .Lwin_loi
    movl %eax, %r12d
.Lwin_pwrite_close:
    movq %r15, %rcx
    call CloseHandle
    testl %eax, %eax
    jnz 1f
    testl %r12d, %r12d
    jns .Lwin_durable_api_error
1:  movl %r12d, %eax
    WIN_RA
.Lwin_fsync:
    movl %r13d, %ecx
    testl %ecx, %ecx
    js .Lwin_durable_bad_fd
    call _get_osfhandle
    cmpq $-2, %rax
    je .Lwin_durable_bad_fd
    cmpq $-1, %rax
    je .Lwin_durable_bad_fd
    movq %rax, %rcx
    call FlushFileBuffers
    testl %eax, %eax
    jz .Lwin_durable_api_error
    xorl %eax, %eax
    WIN_RA
.Lwin_dirsync:
    // Windows has no documented POSIX directory fsync guarantee.
    movl $-129, %eax           // UCRT ENOTSUP
    WIN_RA
.Lwin_durable_bad_fd:
    movl $-9, %eax
    WIN_RA
.Lwin_durable_api_error:
    call .Lwin_loi
    WIN_RA

// LockFileEx holds an exclusive, nonblocking range lock until close.
.Lwin_lock:
    movl %r13d, %ecx
    testl %ecx, %ecx
    js .Lwin_durable_bad_fd
    call _get_osfhandle
    cmpq $-2, %rax
    je .Lwin_durable_bad_fd
    cmpq $-1, %rax
    je .Lwin_durable_bad_fd
    movq %rax, %rcx
    movl $3, %edx
    xorl %r8d, %r8d
    movl $-1, %r9d
    movl $-1, 32(%rsp)
    movq $0, -88(%rbp)
    movq $0, -80(%rbp)
    movq $0, -72(%rbp)
    movq $0, -64(%rbp)
    leaq -88(%rbp), %rax
    movq %rax, 40(%rsp)
    call LockFileEx
    testl %eax, %eax
    jz .Lwin_durable_api_error
    xorl %eax, %eax
    WIN_RA

.Lwin_hardlink:
    cmpl $(VPP_ARENA_SIZE-4096), %r13d
    ja .Lwin_khong_hop_le
    cmpl $(VPP_ARENA_SIZE-4096), %r14d
    ja .Lwin_khong_hop_le
    leaq (%rbx,%r13), %rsi
    leaq (%rbx,%r14), %rdi
    movq %rsi, %rax
    movl $4096, %ecx
1:  cmpb $0, (%rax)
    je 2f
    incq %rax
    decl %ecx
    jnz 1b
    jmp .Lwin_khong_hop_le
2:  movq %rdi, %rax
    movl $4096, %ecx
3:  cmpb $0, (%rax)
    je 4f
    incq %rax
    decl %ecx
    jnz 3b
    jmp .Lwin_khong_hop_le
4:  movl $1, %ecx
    movl $16384, %edx
    call calloc
    testq %rax, %rax
    jz .Lwin_khong_hop_le
    movq %rax, %r15
    movq %rsi, %rcx
    movq %r15, %rdx
    call Lduong_utf8_sang_utf16
    testl %eax, %eax
    jz .Lwin_thu_muc_duong_loi
    movq %rdi, %rcx
    leaq 8192(%r15), %rdx
    call Lduong_utf8_sang_utf16
    testl %eax, %eax
    jz .Lwin_thu_muc_duong_loi
    leaq 8192(%r15), %rcx
    movq %r15, %rdx
    xorl %r8d, %r8d
    call CreateHardLinkW
    jmp .Lwin_thu_muc_doi_ket_qua
