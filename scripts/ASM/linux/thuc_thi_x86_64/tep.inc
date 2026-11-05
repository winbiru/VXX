// Đọc/ghi, mở/đóng và thao tác tệp.

// Nhân Linux trả -errno; V++ cần -1 kèm errno đọc riêng. Tập trung việc đổi lỗi tại đây.
.Lchuan_hoa_goi_he_thong:
    cmpq $-4095, %rax
    jb .Lchuan_hoa_hop_le
    negl %eax
    movl %eax, vpp_linux_errno(%rip)
    movl $-1, %eax
.Lchuan_hoa_hop_le:
    ret
.globl vpp_native_os_errno
vpp_native_os_errno:
    movl vpp_linux_errno(%rip), %eax
    ret

.macro VPP_NHAP_XUAT ten, dich
    .globl \ten
\ten:
    VPP_VAO_BO_NHO
    VPP_KIEM_TRA_LECH %esi, 0
    testl %edx, %edx
    js .Lbo_nho_khong_hop_le
    movl %esi, %eax
    addq %rdx, %rax
    cmpq $VPP_LINUX_ARENA_SIZE, %rax
    ja .Lbo_nho_khong_hop_le
    leaq (%r11,%rsi), %rsi
    call \dich
    VPP_TRA_SYSCALL
.endm
VPP_NHAP_XUAT vpp_native_os_read, vpp_sys_read
VPP_NHAP_XUAT vpp_native_os_write, vpp_sys_write

.globl vpp_native_os_open
vpp_native_os_open:
    VPP_VAO_BO_NHO
    VPP_KIEM_TRA_DUONG %edi
    leaq (%r11,%rdi), %rsi
    movl -8(%rbp), %edx         // Cờ mở tệp được yêu cầu
    cmpl $1537, %edx            // V++ Darwin O_WRONLY|O_CREAT|O_TRUNC
    jne .Lmo_co
    movl $577, %edx             // Linux O_WRONLY|O_CREAT|O_TRUNC
.Lmo_co:
    movq $-100, %rdi            // AT_FDCWD
    movl $438, %ecx             // 0666, chịu ảnh hưởng umask
    call vpp_sys_openat
    VPP_TRA_SYSCALL
.globl vpp_native_os_close
vpp_native_os_close:
    subq $8, %rsp
    call vpp_sys_close
    call .Lchuan_hoa_goi_he_thong
    addq $8, %rsp
    ret

// struct stat nhân Linux x86-64: st_dev@0, st_ino@8, st_mode@24. V++ đọc đầu Darwin: dev 32 bit@0,
// mode@4, inode 64 bit@8. Đọc vào vùng tạm riêng rồi đổi đầu bản ghi để giữ nguyên dữ liệu nhân.
.macro VPP_STAT ten, theo_lien_ket, mo_ta
    .globl \ten
\ten:
    VPP_VAO_BO_NHO
    cmpl $3376, %esi
    je 1f
    cmpl $3584, %esi
    jne .Lbo_nho_khong_hop_le
1:  VPP_KIEM_TRA_LECH %esi, 144
    leaq (%r11,%rsi), %rax
    movq %rax, -32(%rbp)       // Phần đầu kết quả V++
    subq $160, %rsp           // stat Linux 144 byte và phần căn ngăn xếp
    .if \mo_ta
        leaq -208(%rbp), %rsi
        call vpp_sys_fstat
    .else
        VPP_KIEM_TRA_DUONG %edi
        leaq (%r11,%rdi), %rsi
        movq $-100, %rdi
        leaq -208(%rbp), %rdx
        movl $\theo_lien_ket, %ecx
        call vpp_sys_newfstatat
    .endif
    testq %rax, %rax
    js 2f
    movq -32(%rbp), %r10
    movl -208(%rbp), %r9d     // st_dev, 32 bit thấp
    movl %r9d, (%r10)
    movl -184(%rbp), %r9d     // st_mode tại độ lệch Linux +24
    movl %r9d, 4(%r10)
    movq -200(%rbp), %r9      // st_ino tại độ lệch Linux +8
    movq %r9, 8(%r10)
2:
    VPP_TRA_SYSCALL
.endm
VPP_STAT vpp_native_os_stat, 0, 0
VPP_STAT vpp_native_os_lstat, 256, 0
VPP_STAT vpp_native_os_fstat, 0, 1

.globl vpp_native_os_access_exec
vpp_native_os_access_exec:
    VPP_VAO_BO_NHO
    VPP_KIEM_TRA_DUONG %edi
    leaq (%r11,%rdi), %rsi
    movq $-100, %rdi
    movl $1, %edx               // X_OK
    call vpp_sys_faccessat
    VPP_TRA_SYSCALL
.globl vpp_native_os_fchmod
vpp_native_os_fchmod:
    subq $8, %rsp
    call vpp_sys_fchmod
    call .Lchuan_hoa_goi_he_thong
    addq $8, %rsp
    ret
.globl vpp_native_os_unlink
vpp_native_os_unlink:
    VPP_VAO_BO_NHO
    VPP_KIEM_TRA_DUONG %edi
    leaq (%r11,%rdi), %rsi
    movq $-100, %rdi
    xorl %edx, %edx
    call vpp_sys_unlinkat
    VPP_TRA_SYSCALL
.globl vpp_native_os_mkdir
vpp_native_os_mkdir:
    VPP_VAO_BO_NHO
    VPP_KIEM_TRA_DUONG %edi
    leaq (%r11,%rdi), %rsi
    movq $-100, %rdi
    movl $448, %edx             // 0700
    call vpp_sys_mkdirat
    VPP_TRA_SYSCALL
.globl vpp_native_os_rename
vpp_native_os_rename:
    VPP_VAO_BO_NHO
    VPP_KIEM_TRA_DUONG %edi
    VPP_KIEM_TRA_DUONG %esi
    leaq (%r11,%rsi), %rcx
    leaq (%r11,%rdi), %rsi
    movq $-100, %rdi
    movq $-100, %rdx
    call vpp_sys_renameat
    VPP_TRA_SYSCALL

