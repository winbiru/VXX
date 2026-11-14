// Shared Win64 frame: save every nonvolatile integer register; leave shadow
// space and checked local storage for calls into the Windows DLLs.
.text
.macro WIN_VAO
    pushq %rbp
    movq %rsp, %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    pushq %rsi
    pushq %rdi
    subq $136, %rsp
    movl %ecx, %r12d
    movl %edx, %r13d
    movl %r8d, %r14d
    call vpp_native_memory_base
    movq %rax, %rbx
    testq %rax, %rax
    jz .Lwin_khong_hop_le
.endm
.macro WIN_RA
    leaq -56(%rbp), %rsp
    popq %rdi
    popq %rsi
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.endm
// Return normalized errno values rather than mixing Win32 and CRT errors.
.Lwin_loi:
    subq $40, %rsp
    call GetLastError
    addq $40, %rsp
    movl $2, %edx
    cmpl $2, %eax
    je 1f
    cmpl $3, %eax
    je 1f
    movl $17, %edx
    cmpl $183, %eax
    je 1f
    cmpl $80, %eax
    je 1f
    movl $13, %edx
    cmpl $5, %eax
    je 1f
    movl $36, %edx
    cmpl $206, %eax
    je 1f
    movl $5, %edx
1:  movl %edx, %eax
    negl %eax
    ret
.Lwin_khong_hop_le:
    movl $-22, %eax
    WIN_RA

.globl vpp_native_os_clock
vpp_native_os_clock:
    WIN_VAO
    cmpl $1, %r12d
    ja .Lwin_khong_hop_le
    cmpl $(VPP_ARENA_SIZE-16), %r13d
    ja .Lwin_khong_hop_le
    leaq (%rbx,%r13), %r15
    testl %r12d, %r12d
    jnz .Lwin_gio_quoc_te
    leaq -72(%rbp), %rcx
    call QueryPerformanceCounter
    testl %eax, %eax
    jz .Lwin_khong_hop_le
    leaq -80(%rbp), %rcx
    call QueryPerformanceFrequency
    testl %eax, %eax
    jz .Lwin_khong_hop_le
    movq -72(%rbp), %rax
    xorq %rdx, %rdx
    divq -80(%rbp)
    movq %rax, (%r15)
    movq %rdx, %rax
    imulq $1000000000, %rax
    xorq %rdx, %rdx
    divq -80(%rbp)
    movq %rax, 8(%r15)
    xorl %eax, %eax
    WIN_RA
.Lwin_gio_quoc_te:
    leaq -72(%rbp), %rcx
    call GetSystemTimeAsFileTime
    movq -72(%rbp), %rax
    movabsq $116444736000000000, %rcx
    subq %rcx, %rax
.Lwin_filetime_timespec:
    movl $10000000, %ecx
    xorq %rdx, %rdx
    divq %rcx
    movq %rax, (%r15)
    imulq $100, %rdx
    movq %rdx, 8(%r15)
    xorl %eax, %eax
    WIN_RA

.globl vpp_native_os_thread_cpu_clock
vpp_native_os_thread_cpu_clock:
    WIN_VAO
    cmpl $(VPP_ARENA_SIZE-16), %r12d
    ja .Lwin_khong_hop_le
    leaq (%rbx,%r12), %r15
    movq $-2, %rcx
    leaq -72(%rbp), %rdx
    leaq -80(%rbp), %r8
    leaq -88(%rbp), %r9
    leaq -96(%rbp), %rax
    movq %rax, 32(%rsp)
    call GetThreadTimes
    testl %eax, %eax
    jz .Lwin_khong_hop_le
    movq -88(%rbp), %rax
    addq -96(%rbp), %rax
    jmp .Lwin_filetime_timespec

.globl vpp_native_os_sleep
vpp_native_os_sleep:
    cmpl $999, %ecx
    ja 1f
    subq $40, %rsp
    call Sleep
    addq $40, %rsp
    xorl %eax, %eax
    ret
1:  movl $22, %eax
    ret

.globl vpp_native_os_entropy
vpp_native_os_entropy:
    WIN_VAO
    cmpl $256, %r13d
    ja .Lwin_khong_hop_le
    cmpl $(VPP_ARENA_SIZE-256), %r12d
    ja .Lwin_khong_hop_le
    xorl %ecx, %ecx
    leaq (%rbx,%r12), %rdx
    movl %r13d, %r8d
    movl $2, %r9d
    call BCryptGenRandom
    testl %eax, %eax
    js 1f
    movl %r13d, %eax
    WIN_RA
1:  movl $-5, %eax
    WIN_RA

.globl vpp_native_os_identity
vpp_native_os_identity:
    cmpl $5, %ecx
    ja 2f
    testl %ecx, %ecx
    jz GetCurrentProcessId
    cmpl $1, %ecx
    je 1f
    // Windows has no POSIX uid/gid namespace. Match the CRT compatibility
    // identity for these fields; process identity remains the real PID.
    xorl %eax, %eax
    ret
1:  subq $104, %rsp
    movq $-1, %rcx
    xorl %edx, %edx
    leaq 48(%rsp), %r8
    movl $48, %r9d
    movq $0, 32(%rsp)
    call NtQueryInformationProcess
    testl %eax, %eax
    js 3f
    movl 88(%rsp), %eax
    addq $104, %rsp
    ret
3:  addq $104, %rsp
2:  movl $-22, %eax
    ret
