// Đồng hồ, ngủ và ngẫu nhiên POSIX cho bộ thực thi V++ độc lập. Đối số là độ lệch i32; ngủ trả errno
// cùng thao tác, ngẫu nhiên trả số byte hoặc -errno.
#if defined(__APPLE__)
#define VM_SYS_SYM(x) _##x
#define VM_SYS_LIB(x) _##x
#else
#define VM_SYS_SYM(x) x
#define VM_SYS_LIB(x) x@PLT
#endif
#ifdef VPP_VM_SYSTEM_FREESTANDING
#define VM_SYS_X86_ARENA 135271424
#else
#define VM_SYS_X86_ARENA 68162560
#endif
.text
#if defined(__aarch64__) || defined(__arm64__)
.macro VM_VAO_HE_THONG
    stp x19, x20, [sp, #-64]!
    stp x21, x30, [sp, #16]
    mov w19, w0
    mov w20, w1
    bl VM_SYS_SYM(vpp_native_memory_base)
    mov x21, x0
    cbz x21, Lvm_he_thong_sai_bat
.endm
.macro VM_TRA_HE_THONG
    ldp x21, x30, [sp, #16]
    ldp x19, x20, [sp], #64
    ret
.endm
.globl VM_SYS_SYM(vpp_native_os_clock)
VM_SYS_SYM(vpp_native_os_clock):
    VM_VAO_HE_THONG
    cmp w19, #1                // 0 = đơn điệu, 1 = giờ quốc tế
    b.hi Lvm_he_thong_sai_bat
    tbnz w20, #31, Lvm_he_thong_sai_bat
    mov w10, #0x13f0           // Kích thước vùng nhớ trừ sizeof(timespec)
    movk w10, #0x410, lsl #16
    cmp w20, w10
    b.hi Lvm_he_thong_sai_bat
    add x1, x21, w20, uxtw
    mov w0, #0
    cbnz w19, 1f
#if defined(__APPLE__)
    mov w0, #6                // CLOCK_MONOTONIC của Darwin
#else
    mov w0, #1
#endif
1:
#if defined(__APPLE__)
    bl _clock_gettime
#else
    mov x8, #113              // clock_gettime
    svc #0
#endif
    VM_TRA_HE_THONG

// Đồng hồ đo CPU không cấp phát: timespec 16 byte tại độ lệch đã kiểm tra. Luôn đọc thời gian CPU
// của luồng hiện tại.
.globl VM_SYS_SYM(vpp_native_os_thread_cpu_clock)
VM_SYS_SYM(vpp_native_os_thread_cpu_clock):
    mov w1, w0
    mov w0, #0
    VM_VAO_HE_THONG
    tbnz w20, #31, Lvm_he_thong_sai_bat
    mov w10, #0x13f0
    movk w10, #0x410, lsl #16
    cmp w20, w10
    b.hi Lvm_he_thong_sai_bat
    add x1, x21, w20, uxtw
#if defined(__APPLE__)
    mov w0, #16               // CLOCK_THREAD_CPUTIME_ID của Darwin
    bl _clock_gettime
#else
    mov w0, #3                // CLOCK_THREAD_CPUTIME_ID của Linux
    mov x8, #113
    svc #0
#endif
    VM_TRA_HE_THONG

.globl VM_SYS_SYM(vpp_native_os_sleep)
VM_SYS_SYM(vpp_native_os_sleep):
    VM_VAO_HE_THONG
    cmp w19, #999
    b.hi Lvm_he_thong_ngu_sai_bat
    cbz w19, Lvm_he_thong_ngu_hop_le_bat
#if defined(__APPLE__)
    mov x0, #0
    mov x1, #0
    mov w2, w19
    mov x16, #230             // poll(NULL, 0, milliseconds)
    svc #0x80
    b.cc Lvm_he_thong_ngu_hop_le_bat
#else
    str xzr, [sp, #32]        // Thử một lần; V++ quản lý thời hạn khi gặp EINTR
    mov x10, #16960
    movk x10, #15, lsl #16    // 1,000,000 ns/ms
    uxtw x19, w19
    mul x10, x19, x10
    str x10, [sp, #40]
    add x0, sp, #32
    mov x1, #0
    mov x8, #101              // nanosleep
    svc #0
    cmp x0, #0
    b.ge Lvm_he_thong_ngu_hop_le_bat
    neg w0, w0
#endif
    VM_TRA_HE_THONG
Lvm_he_thong_ngu_hop_le_bat:
    mov w0, #0
    VM_TRA_HE_THONG
Lvm_he_thong_ngu_sai_bat:
    mov w0, #22
    VM_TRA_HE_THONG

.globl VM_SYS_SYM(vpp_native_os_entropy)
VM_SYS_SYM(vpp_native_os_entropy):
    VM_VAO_HE_THONG
    sub w10, w20, #1
    cmp w10, #255
    b.hi Lvm_he_thong_sai_bat
    tbnz w19, #31, Lvm_he_thong_sai_bat
    uxtw x10, w19
    add x10, x10, w20, uxtw
    mov w11, #0x1400
    movk w11, #0x410, lsl #16
    cmp x10, x11
    b.hi Lvm_he_thong_sai_bat
    add x0, x21, w19, uxtw
    uxtw x1, w20
#if defined(__APPLE__)
    bl _getentropy
    cbnz w0, 1f
    mov w0, w20
    VM_TRA_HE_THONG
1:  bl ___error
    ldr w0, [x0]
    neg w0, w0
#else
    mov x2, #0
    mov x8, #278              // getrandom
    svc #0
#endif
    VM_TRA_HE_THONG
Lvm_he_thong_sai_bat:
    mov w0, #-22
    VM_TRA_HE_THONG

#elif defined(__x86_64__)
.macro VM_VAO_HE_THONG
    pushq %rbp
    movq %rsp, %rbp
    subq $48, %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    call VM_SYS_SYM(vpp_native_memory_base)
    testq %rax, %rax
    jz Lvm_he_thong_sai_x86
    movq %rax, %r10
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
.endm
.macro VM_TRA_HE_THONG
    leave
    ret
.endm
.globl VM_SYS_SYM(vpp_native_os_clock)
VM_SYS_SYM(vpp_native_os_clock):
    VM_VAO_HE_THONG
    cmpl $1, %edi
    ja Lvm_he_thong_sai_x86
    cmpl $(VM_SYS_X86_ARENA - 16), %esi
    ja Lvm_he_thong_sai_x86
    addq %r10, %rsi
    testl %edi, %edi
    movl $0, %edi
    jnz 1f
#if defined(__APPLE__)
    movl $6, %edi
#else
    movl $1, %edi
#endif
1:
#if defined(__APPLE__)
    call VM_SYS_LIB(clock_gettime)
#else
    movl $228, %eax
    syscall
#endif
    VM_TRA_HE_THONG

.globl VM_SYS_SYM(vpp_native_os_thread_cpu_clock)
VM_SYS_SYM(vpp_native_os_thread_cpu_clock):
    movl %edi, %esi
    xorl %edi, %edi
    VM_VAO_HE_THONG
    cmpl $(VM_SYS_X86_ARENA - 16), %esi
    ja Lvm_he_thong_sai_x86
    addq %r10, %rsi
#if defined(__APPLE__)
    movl $16, %edi
    call VM_SYS_LIB(clock_gettime)
#else
    movl $3, %edi
    movl $228, %eax
    syscall
#endif
    VM_TRA_HE_THONG

.globl VM_SYS_SYM(vpp_native_os_sleep)
VM_SYS_SYM(vpp_native_os_sleep):
    VM_VAO_HE_THONG
    cmpl $999, %edi
    ja Lvm_he_thong_ngu_sai_x86
    testl %edi, %edi
    jz Lvm_he_thong_ngu_hop_le_x86
#if defined(__APPLE__)
    movl %edi, %edx
    xorl %edi, %edi
    xorl %esi, %esi
    movl $0x20000e6, %eax     // poll
    syscall
    jnc Lvm_he_thong_ngu_hop_le_x86
#else
    imulq $1000000, %rdi, %rax
    movq $0, -32(%rbp)
    movq %rax, -24(%rbp)
    leaq -32(%rbp), %rdi
    xorl %esi, %esi
    movl $35, %eax            // nanosleep
    syscall
    testq %rax, %rax
    jns Lvm_he_thong_ngu_hop_le_x86
    negl %eax
#endif
    VM_TRA_HE_THONG
Lvm_he_thong_ngu_hop_le_x86:
    xorl %eax, %eax
    VM_TRA_HE_THONG
Lvm_he_thong_ngu_sai_x86:
    movl $22, %eax
    VM_TRA_HE_THONG

.globl VM_SYS_SYM(vpp_native_os_entropy)
VM_SYS_SYM(vpp_native_os_entropy):
    VM_VAO_HE_THONG
    leal -1(%rsi), %eax
    cmpl $255, %eax
    ja Lvm_he_thong_sai_x86
    testl %edi, %edi
    js Lvm_he_thong_sai_x86
    movl %edi, %eax
    addq %rsi, %rax
    cmpq $VM_SYS_X86_ARENA, %rax
    ja Lvm_he_thong_sai_x86
    addq %r10, %rdi
#if defined(__APPLE__)
    call VM_SYS_LIB(getentropy)
    testl %eax, %eax
    jnz 1f
    movl -8(%rbp), %eax
    VM_TRA_HE_THONG
1:  call VM_SYS_LIB(__error)
    movl (%rax), %eax
    negl %eax
#else
    xorl %edx, %edx
    movl $318, %eax           // getrandom
    syscall
#endif
    VM_TRA_HE_THONG
Lvm_he_thong_sai_x86:
    movl $-22, %eax
    VM_TRA_HE_THONG
#endif
#undef VM_SYS_SYM
#undef VM_SYS_LIB
