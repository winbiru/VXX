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
    cmpl $33, %eax
    je 1f
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
    cmpl $2, %r12d
    je .Lwin_gio_dia_phuong
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

// kind=2: signed Unix seconds in, normalized 64-byte tm out.
// Use Win32 calendar conversion (including epochs before 1970), not CRT.
.Lwin_gio_dia_phuong:
    cmpl $(VPP_ARENA_SIZE-64), %r13d
    ja .Lwin_khong_hop_le
    leaq (%rbx,%r13), %r15
    movq (%r15), %rax
    movabsq $-11644473600, %rcx
    cmpq %rcx, %rax
    jl .Lwin_khong_hop_le
    movabsq $253402300799, %rcx
    cmpq %rcx, %rax
    jg .Lwin_khong_hop_le
    imulq $10000000, %rax, %rax
    movabsq $116444736000000000, %rcx
    addq %rcx, %rax
    movq %rax, -72(%rbp)
    leaq -72(%rbp), %rcx
    leaq -88(%rbp), %rdx
    call FileTimeToSystemTime
    testl %eax, %eax
    jz .Lwin_khong_hop_le
    xorl %ecx, %ecx
    leaq -88(%rbp), %rdx
    leaq -104(%rbp), %r8
    call SystemTimeToTzSpecificLocalTimeEx
    testl %eax, %eax
    jz .Lwin_khong_hop_le
    leaq -104(%rbp), %rcx
    leaq -112(%rbp), %rdx
    call SystemTimeToFileTime
    testl %eax, %eax
    jz .Lwin_khong_hop_le
    movq -112(%rbp), %rax
    subq -72(%rbp), %rax
    cqto
    movl $10000000, %ecx
    idivq %rcx
    movq %rax, 40(%r15)
    movzwl -92(%rbp), %eax
    movl %eax, 0(%r15)
    movzwl -94(%rbp), %eax
    movl %eax, 4(%r15)
    movzwl -96(%rbp), %eax
    movl %eax, 8(%r15)
    movzwl -98(%rbp), %eax
    movl %eax, 12(%r15)
    movzwl -102(%rbp), %eax
    decl %eax
    movl %eax, 16(%r15)
    movzwl -104(%rbp), %eax
    subl $1900, %eax
    movl %eax, 20(%r15)
    movzwl -100(%rbp), %eax
    movl %eax, 24(%r15)
    // V++ derives yday and validates the normalized calendar.
    movl $0, 28(%r15)
    movl $-1, 32(%r15)
    movl $0, 36(%r15)
    movq $0, 48(%r15)
    movq $0, 56(%r15)
    // Get the rules for the actual local year; never use today's DST flag.
    subq $224, %rsp
    movzwl -104(%rbp), %ecx
    xorl %edx, %edx
    leaq 32(%rsp), %r8
    call GetTimeZoneInformationForYear
    testl %eax, %eax
    jz 1f
    movl 32(%rsp), %eax
    addl 116(%rsp), %eax        // Bias + StandardBias
    imull $-60, %eax
    movl 32(%rsp), %edx
    addl 200(%rsp), %edx        // Bias + DaylightBias
    imull $-60, %edx
    movl $0, 32(%r15)
    cmpl %eax, %edx
    je 1f
    cmpl %edx, 40(%r15)
    jne 1f
    movl $1, 32(%r15)
1:
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
    testl %r13d, %r13d
    jz .Lwin_khong_hop_le
    cmpl $256, %r13d
    ja .Lwin_khong_hop_le
    movl %r12d, %eax
    addq %r13, %rax
    cmpq $VPP_ARENA_SIZE, %rax
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
