// Hàm bọc syscall Linux theo quy ước thanh ghi SysV.

.text
.p2align 4
.globl vpp_sys_read
.type vpp_sys_read, @function
vpp_sys_read:
    movl $0, %eax
    syscall
    ret
.size vpp_sys_read, .-vpp_sys_read
.globl vpp_sys_write
.type vpp_sys_write, @function
vpp_sys_write:
    movl $1, %eax
    syscall
    ret
.size vpp_sys_write, .-vpp_sys_write
.globl vpp_sys_close
.type vpp_sys_close, @function
vpp_sys_close:
    movl $3, %eax
    syscall
    ret
.size vpp_sys_close, .-vpp_sys_close
.globl vpp_sys_fstat
.type vpp_sys_fstat, @function
vpp_sys_fstat:
    movl $5, %eax
    syscall
    ret
.size vpp_sys_fstat, .-vpp_sys_fstat
.globl vpp_sys_lseek
.type vpp_sys_lseek, @function
vpp_sys_lseek:
    movl $8, %eax
    syscall
    ret
.size vpp_sys_lseek, .-vpp_sys_lseek
.globl vpp_sys_munmap
.type vpp_sys_munmap, @function
vpp_sys_munmap:
    movl $11, %eax
    syscall
    ret
.size vpp_sys_munmap, .-vpp_sys_munmap
.globl vpp_sys_nanosleep
.type vpp_sys_nanosleep, @function
vpp_sys_nanosleep:
    movl $35, %eax
    syscall
    ret
.size vpp_sys_nanosleep, .-vpp_sys_nanosleep
.globl vpp_sys_getpid
.type vpp_sys_getpid, @function
vpp_sys_getpid:
    movl $39, %eax
    syscall
    ret
.size vpp_sys_getpid, .-vpp_sys_getpid
.globl vpp_sys_fork
.type vpp_sys_fork, @function
vpp_sys_fork:
    movl $57, %eax
    syscall
    ret
.size vpp_sys_fork, .-vpp_sys_fork
.globl vpp_sys_execve
.type vpp_sys_execve, @function
vpp_sys_execve:
    movl $59, %eax
    syscall
    ret
.size vpp_sys_execve, .-vpp_sys_execve
.globl vpp_sys_kill
.type vpp_sys_kill, @function
vpp_sys_kill:
    movl $62, %eax
    syscall
    ret
.size vpp_sys_kill, .-vpp_sys_kill
.globl vpp_sys_fcntl
.type vpp_sys_fcntl, @function
vpp_sys_fcntl:
    movl $72, %eax
    syscall
    ret
.size vpp_sys_fcntl, .-vpp_sys_fcntl
.globl vpp_sys_fchmod
.type vpp_sys_fchmod, @function
vpp_sys_fchmod:
    movl $91, %eax
    syscall
    ret
.size vpp_sys_fchmod, .-vpp_sys_fchmod
.globl vpp_sys_setpgid
.type vpp_sys_setpgid, @function
vpp_sys_setpgid:
    movl $109, %eax
    syscall
    ret
.size vpp_sys_setpgid, .-vpp_sys_setpgid
.globl vpp_sys_getpgid
.type vpp_sys_getpgid, @function
vpp_sys_getpgid:
    movl $121, %eax
    syscall
    ret
.size vpp_sys_getpgid, .-vpp_sys_getpgid
.globl vpp_sys_dup2
.type vpp_sys_dup2, @function
vpp_sys_dup2:
    movl $33, %eax
    syscall
    ret
.size vpp_sys_dup2, .-vpp_sys_dup2
.globl vpp_sys_poll
.type vpp_sys_poll, @function
vpp_sys_poll:
    movl $7, %eax
    syscall
    ret
.size vpp_sys_poll, .-vpp_sys_poll
.globl vpp_sys_pipe2
.type vpp_sys_pipe2, @function
vpp_sys_pipe2:
    movl $293, %eax
    syscall
    ret
.size vpp_sys_pipe2, .-vpp_sys_pipe2
.globl vpp_sys_getcwd
.type vpp_sys_getcwd, @function
vpp_sys_getcwd:
    movl $79, %eax
    syscall
    ret
.size vpp_sys_getcwd, .-vpp_sys_getcwd
.globl vpp_sys_clock_gettime
.type vpp_sys_clock_gettime, @function
vpp_sys_clock_gettime:
    movl $228, %eax
    syscall
    ret
.size vpp_sys_clock_gettime, .-vpp_sys_clock_gettime
.globl vpp_sys_exit_group
.type vpp_sys_exit_group, @function
vpp_sys_exit_group:
    movl $231, %eax
    syscall
    ret
.size vpp_sys_exit_group, .-vpp_sys_exit_group
.globl vpp_sys_openat
.type vpp_sys_openat, @function
vpp_sys_openat:
    movq %rcx, %r10
    movl $257, %eax
    syscall
    ret
.size vpp_sys_openat, .-vpp_sys_openat
.globl vpp_sys_mkdirat
.type vpp_sys_mkdirat, @function
vpp_sys_mkdirat:
    movq %rcx, %r10
    movl $258, %eax
    syscall
    ret
.size vpp_sys_mkdirat, .-vpp_sys_mkdirat
.globl vpp_sys_newfstatat
.type vpp_sys_newfstatat, @function
vpp_sys_newfstatat:
    movq %rcx, %r10
    movl $262, %eax
    syscall
    ret
.size vpp_sys_newfstatat, .-vpp_sys_newfstatat
.globl vpp_sys_unlinkat
.type vpp_sys_unlinkat, @function
vpp_sys_unlinkat:
    movq %rcx, %r10
    movl $263, %eax
    syscall
    ret
.size vpp_sys_unlinkat, .-vpp_sys_unlinkat
.globl vpp_sys_renameat
.type vpp_sys_renameat, @function
vpp_sys_renameat:
    movq %rcx, %r10
    movl $264, %eax
    syscall
    ret
.size vpp_sys_renameat, .-vpp_sys_renameat
.globl vpp_sys_readlinkat
.type vpp_sys_readlinkat, @function
vpp_sys_readlinkat:
    movq %rcx, %r10
    movl $267, %eax
    syscall
    ret
.size vpp_sys_readlinkat, .-vpp_sys_readlinkat
.globl vpp_sys_faccessat
.type vpp_sys_faccessat, @function
vpp_sys_faccessat:
    movq %rcx, %r10
    movl $269, %eax
    syscall
    ret
.size vpp_sys_faccessat, .-vpp_sys_faccessat
.globl vpp_sys_wait4
.type vpp_sys_wait4, @function
vpp_sys_wait4:
    movq %rcx, %r10
    movl $61, %eax
    syscall
    ret
.size vpp_sys_wait4, .-vpp_sys_wait4
.globl vpp_sys_getrandom
.type vpp_sys_getrandom, @function
vpp_sys_getrandom:
    movl $318, %eax
    syscall
    ret
.size vpp_sys_getrandom, .-vpp_sys_getrandom
.globl vpp_sys_socket
.type vpp_sys_socket, @function
vpp_sys_socket:
    movl $41, %eax
    syscall
    ret
.size vpp_sys_socket, .-vpp_sys_socket
.globl vpp_sys_connect
.type vpp_sys_connect, @function
vpp_sys_connect:
    movl $42, %eax
    syscall
    ret
.size vpp_sys_connect, .-vpp_sys_connect
.globl vpp_sys_accept
.type vpp_sys_accept, @function
vpp_sys_accept:
    movl $43, %eax
    syscall
    ret
.size vpp_sys_accept, .-vpp_sys_accept
.globl vpp_sys_sendto
.type vpp_sys_sendto, @function
vpp_sys_sendto:
    movq %rcx, %r10
    movl $44, %eax
    syscall
    ret
.size vpp_sys_sendto, .-vpp_sys_sendto
.globl vpp_sys_recvfrom
.type vpp_sys_recvfrom, @function
vpp_sys_recvfrom:
    movq %rcx, %r10
    movl $45, %eax
    syscall
    ret
.size vpp_sys_recvfrom, .-vpp_sys_recvfrom
.globl vpp_sys_bind
.type vpp_sys_bind, @function
vpp_sys_bind:
    movl $49, %eax
    syscall
    ret
.size vpp_sys_bind, .-vpp_sys_bind
.globl vpp_sys_listen
.type vpp_sys_listen, @function
vpp_sys_listen:
    movl $50, %eax
    syscall
    ret
.size vpp_sys_listen, .-vpp_sys_listen
.globl vpp_sys_getsockname
.type vpp_sys_getsockname, @function
vpp_sys_getsockname:
    movl $51, %eax
    syscall
    ret
.size vpp_sys_getsockname, .-vpp_sys_getsockname
.globl vpp_sys_setsockopt
.type vpp_sys_setsockopt, @function
vpp_sys_setsockopt:
    movq %rcx, %r10
    movl $54, %eax
    syscall
    ret
.size vpp_sys_setsockopt, .-vpp_sys_setsockopt
.globl vpp_sys_getsockopt
.type vpp_sys_getsockopt, @function
vpp_sys_getsockopt:
    movq %rcx, %r10
    movl $55, %eax
    syscall
    ret
.size vpp_sys_getsockopt, .-vpp_sys_getsockopt
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
    movq 8(%rsp), %r9
    movl $9, %eax
    syscall
    ret
.size vpp_sys_mmap, .-vpp_sys_mmap
.section .note.GNU-stack,"",@progbits
