// Phân loại đường dẫn: (đường, struct stat *đích) trả kết quả syscall. Darwin gọi stat/lstat của
// libSystem; Linux gọi newfstatat. Bộ chuyển đổi VM giải mã bố trí stat và coi kết quả khác 0 là
// không tồn tại.
#if defined(__APPLE__)
#define VPP_STAT _vpp_machine_path_stat
#define VPP_LSTAT _vpp_machine_path_lstat
#define HOST_STAT _stat
#define HOST_LSTAT _lstat
#elif defined(__linux__)
#define VPP_STAT vpp_machine_path_stat
#define VPP_LSTAT vpp_machine_path_lstat
#else
#error Kiến trúc máy đường dẫn chưa được hỗ trợ
#endif
.text
.globl VPP_STAT
.globl VPP_LSTAT
.p2align 2
VPP_STAT:
#if defined(__linux__)
#if defined(__aarch64__)
    mov x2, x1              // struct stat *
    mov x1, x0              // Đường dẫn
    mov x0, #-100           // AT_FDCWD
    mov x3, #0              // Theo liên kết tượng trưng
    mov x8, #79             // __NR_newfstatat (aarch64)
    svc #0
#elif defined(__x86_64__)
    movq %rsi, %rdx         // struct stat *
    movq %rdi, %rsi         // Đường dẫn
    movq $-100, %rdi        // AT_FDCWD
    xorl %r10d, %r10d      // Theo liên kết tượng trưng
    movl $262, %eax         // __NR_newfstatat (x86_64)
    syscall
#else
#error Kiến trúc đường dẫn Linux chưa được hỗ trợ
#endif
    ret
#else
#if defined(__aarch64__)
    b HOST_STAT
#else
    jmp HOST_STAT
#endif
#endif
.p2align 2
VPP_LSTAT:
#if defined(__linux__)
#if defined(__aarch64__)
    mov x2, x1
    mov x1, x0
    mov x0, #-100
    mov x3, #256            // AT_SYMLINK_NOFOLLOW
    mov x8, #79
    svc #0
#elif defined(__x86_64__)
    movq %rsi, %rdx
    movq %rdi, %rsi
    movq $-100, %rdi
    movl $256, %r10d        // AT_SYMLINK_NOFOLLOW
    movl $262, %eax
    syscall
#else
#error Kiến trúc đường dẫn Linux chưa được hỗ trợ
#endif
    ret
#else
#if defined(__aarch64__)
    b HOST_LSTAT
#else
    jmp HOST_LSTAT
#endif
#endif
#if defined(__linux__)
#if defined(__aarch64__)
.section .note.GNU-stack,"",%progbits
#else
.section .note.GNU-stack,"",@progbits
#endif
#endif
