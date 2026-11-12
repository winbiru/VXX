// Ranh giới syscall Linux x86-64 độc lập. Đối số theo SysV; kết quả thô >= 0 khi thành công, -errno
// khi lỗi. syscall ghi đè rcx/r11; chuyển đối số thứ tư sang r10.
#if !defined(__linux__) || !defined(__x86_64__)
#error Ranh giới syscall này chỉ hỗ trợ ELF Linux x86-64
#endif

.text
.p2align 4
.macro VPP_SYSCALL ten, so
    .globl vpp_sys_\ten
    .type vpp_sys_\ten, @function
vpp_sys_\ten:
    movl $\so, %eax
    syscall
    ret
    .size vpp_sys_\ten, .-vpp_sys_\ten
.endm
.macro VPP_SYSCALL4 ten, so
    .globl vpp_sys_\ten
    .type vpp_sys_\ten, @function
vpp_sys_\ten:
    movq %rcx, %r10
    movl $\so, %eax
    syscall
    ret
    .size vpp_sys_\ten, .-vpp_sys_\ten
.endm

VPP_SYSCALL read, 0
VPP_SYSCALL write, 1
VPP_SYSCALL close, 3
VPP_SYSCALL fstat, 5
VPP_SYSCALL lseek, 8
VPP_SYSCALL munmap, 11
VPP_SYSCALL nanosleep, 35
VPP_SYSCALL getpid, 39
VPP_SYSCALL fork, 57
VPP_SYSCALL execve, 59
VPP_SYSCALL kill, 62
VPP_SYSCALL fcntl, 72
VPP_SYSCALL fchmod, 91
VPP_SYSCALL setpgid, 109
VPP_SYSCALL getpgid, 121
VPP_SYSCALL dup2, 33
VPP_SYSCALL poll, 7
VPP_SYSCALL pipe2, 293
VPP_SYSCALL getcwd, 79
VPP_SYSCALL clock_gettime, 228
VPP_SYSCALL exit_group, 231
VPP_SYSCALL4 openat, 257
VPP_SYSCALL4 mkdirat, 258
VPP_SYSCALL4 newfstatat, 262
VPP_SYSCALL4 unlinkat, 263
VPP_SYSCALL4 renameat, 264
VPP_SYSCALL4 readlinkat, 267
VPP_SYSCALL4 faccessat, 269
VPP_SYSCALL4 wait4, 61
VPP_SYSCALL getrandom, 318
// Syscall ổ cắm Linux x86-64 gọi trực tiếp, không qua socketcall.
VPP_SYSCALL socket, 41
VPP_SYSCALL connect, 42
VPP_SYSCALL accept, 43
VPP_SYSCALL4 sendto, 44
VPP_SYSCALL4 recvfrom, 45
VPP_SYSCALL bind, 49
VPP_SYSCALL listen, 50
VPP_SYSCALL getsockname, 51
VPP_SYSCALL4 setsockopt, 54
VPP_SYSCALL4 getsockopt, 55

// send/recv dùng sendto/recvfrom với địa chỉ rỗng. VM truyền bốn đối số SysV: mô tả, đệm, độ dài,
// cờ.
.globl vpp_sys_send
.type vpp_sys_send, @function
vpp_sys_send:
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    jmp vpp_sys_sendto
.size vpp_sys_send, .-vpp_sys_send
.globl vpp_sys_recv
.type vpp_sys_recv, @function
vpp_sys_recv:
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    jmp vpp_sys_recvfrom
.size vpp_sys_recv, .-vpp_sys_recv

.globl vpp_sys_mmap
.type vpp_sys_mmap, @function
vpp_sys_mmap:
    movq %rcx, %r10
    movq 8(%rsp), %r9            // Đối số syscall thứ sáu của hàm bọc SysV nằm trên ngăn xếp
    movl $9, %eax
    syscall
    ret
.size vpp_sys_mmap, .-vpp_sys_mmap

.section .note.GNU-stack,"",@progbits
