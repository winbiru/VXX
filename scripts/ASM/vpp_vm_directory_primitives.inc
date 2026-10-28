// i32 arena ABI: identity(kind), directory(op, control, payload).
// Directory op 0=open(path,slot16), 1=read(slot16,buffer8192),
// 2=close(slot16,unused), 3=rmdir(path,unused), 4=mkdir(path,0777).
// Only this boundary handles native DIR pointers; record parsing is V++.
#if defined(__APPLE__)
#define VM_DIR_SYM(x) _##x
#define VM_DIR_LIB(x) _##x
#else
#define VM_DIR_SYM(x) x
#define VM_DIR_LIB(x) x@PLT
#endif
.text
#if defined(__aarch64__) || defined(__arm64__)
.globl VM_DIR_SYM(vpp_native_os_identity)
VM_DIR_SYM(vpp_native_os_identity):
    cmp w0, #5
    b.hi Lvm_identity_bad_arm
#if defined(__APPLE__)
    mov x16, #20              // getpid
    cbz w0, 1f
    mov x16, #39              // getppid
    cmp w0, #1
    b.eq 1f
    mov x16, #24              // getuid
    cmp w0, #2
    b.eq 1f
    mov x16, #25              // geteuid
    cmp w0, #3
    b.eq 1f
    mov x16, #47              // getgid
    cmp w0, #4
    b.eq 1f
    mov x16, #43              // getegid
1:  svc #0x80
    b.cc 2f
    neg w0, w0
#else
    mov x8, #172
    cbz w0, 1f
    mov x8, #173
    cmp w0, #1
    b.eq 1f
    mov x8, #174              // getuid
    cmp w0, #2
    b.eq 1f
    mov x8, #175              // geteuid
    cmp w0, #3
    b.eq 1f
    mov x8, #176              // getgid
    cmp w0, #4
    b.eq 1f
    mov x8, #177              // getegid
1:  svc #0
#endif
2:  ret
Lvm_identity_bad_arm:
    mov w0, #-22
    ret

.macro VM_DIR_ENTER
    stp x19, x20, [sp, #-80]!
    stp x21, x22, [sp, #16]
    stp x23, x30, [sp, #32]
    mov w19, w0
    mov w20, w1
    mov w22, w2
    bl VM_DIR_SYM(vpp_native_memory_base)
    mov x21, x0
    cbz x21, Lvm_dir_bad_arm
.endm
.macro VM_DIR_RETURN
    ldp x23, x30, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #80
    ret
.endm
.globl VM_DIR_SYM(vpp_native_os_directory)
VM_DIR_SYM(vpp_native_os_directory):
    VM_DIR_ENTER
    cmp w19, #4
    b.hi Lvm_dir_bad_arm
    cbz w19, Lvm_dir_path_arm
    cmp w19, #3
    b.hs Lvm_dir_path_arm
    // Control blocks hold a full 64-bit OS handle and a live marker.
    tbnz w20, #31, Lvm_dir_bad_arm
    tst w20, #7
    b.ne Lvm_dir_bad_arm
    mov w10, #0x13f0
    movk w10, #0x410, lsl #16
    cmp w20, w10
    b.hi Lvm_dir_bad_arm
    add x23, x21, w20, uxtw
    ldr w10, [x23, #8]
    mov w11, #0x4944
    movk w11, #0x4452, lsl #16
    cmp w10, w11
    b.ne Lvm_dir_closed_arm
    cmp w19, #2
    b.eq Lvm_dir_close_arm
    tbnz w22, #31, Lvm_dir_bad_arm
    mov w10, #0xf400           // arena size - 8192 = 0x40ff400
    movk w10, #0x40f, lsl #16
    cmp w22, w10
    b.hi Lvm_dir_bad_arm
    add x22, x21, w22, uxtw
#if defined(__APPLE__)
    bl ___error
    str wzr, [x0]
    ldr x0, [x23]
    bl _readdir
    cbz x0, Lvm_dir_errno_arm // errno=0 distinguishes EOF
    ldrh w19, [x0, #16]
    cmp w19, #22
    b.lo Lvm_dir_io_arm
    cmp w19, #1048
    b.hi Lvm_dir_io_arm
    mov x1, x0
    mov x0, x22
    uxtw x2, w19
    bl _memcpy                // copy opaque dirent bytes, decode in V++
    mov w0, w19
#else
    ldr x0, [x23]
    mov x1, x22
    mov x2, #8192
    mov x8, #61               // getdents64
    svc #0
#endif
    VM_DIR_RETURN
Lvm_dir_close_arm:
    ldr x0, [x23]
    str wzr, [x23, #8]       // revoke even when OS close fails
    str xzr, [x23]
#if defined(__APPLE__)
    bl _closedir
    cbnz w0, Lvm_dir_errno_arm
#else
    mov x8, #57
    svc #0
#endif
    VM_DIR_RETURN
Lvm_dir_path_arm:
    tbnz w20, #31, Lvm_dir_bad_arm
    mov w10, #0x400
    movk w10, #0x410, lsl #16 // arena size - 4096
    cmp w20, w10
    b.hi Lvm_dir_bad_arm
    add x20, x21, w20, uxtw
    cmp w19, #4
    b.eq Lvm_dir_mkdir_arm
    cmp w19, #3
    b.eq Lvm_dir_remove_arm
    tbnz w22, #31, Lvm_dir_bad_arm
    tst w22, #7
    b.ne Lvm_dir_bad_arm
    mov w10, #0x13f0
    movk w10, #0x410, lsl #16
    cmp w22, w10
    b.hi Lvm_dir_bad_arm
    add x23, x21, w22, uxtw
    str wzr, [x23, #8]
#if defined(__APPLE__)
    mov x0, x20
    mov w1, #0
    mov x2, #4096
    bl _memchr
    cbz x0, Lvm_dir_bad_arm
    mov x0, x20
    bl _opendir
    cbz x0, Lvm_dir_errno_arm
#else
    mov x0, #-100
    mov x1, x20
    mov x2, #0x90000          // O_DIRECTORY | O_CLOEXEC
    mov x3, #0
    mov x8, #56               // openat
    svc #0
    tbnz x0, #63, Lvm_dir_return_arm
#endif
    str x0, [x23]
    mov w10, #0x4944
    movk w10, #0x4452, lsl #16
    str w10, [x23, #8]
    mov w0, #0
Lvm_dir_return_arm:
    VM_DIR_RETURN
Lvm_dir_remove_arm:
#if defined(__APPLE__)
    mov x0, x20
    mov x16, #137             // rmdir
    svc #0x80
    b.cc Lvm_dir_return_arm
    neg w0, w0
#else
    mov x0, #-100
    mov x1, x20
    mov x2, #512              // AT_REMOVEDIR
    mov x8, #35               // unlinkat
    svc #0
#endif
    VM_DIR_RETURN
Lvm_dir_mkdir_arm:
#if defined(__APPLE__)
    mov x0, x20
    mov x1, #511
    mov x16, #136             // mkdir, mode 0777 subject to umask
    svc #0x80
    b.cc Lvm_dir_return_arm
    neg w0, w0
#else
    mov x0, #-100
    mov x1, x20
    mov x2, #511
    mov x8, #34               // mkdirat
    svc #0
#endif
    VM_DIR_RETURN
#if defined(__APPLE__)
Lvm_dir_errno_arm:
    bl ___error
    ldr w0, [x0]
    neg w0, w0
    VM_DIR_RETURN
#endif
Lvm_dir_closed_arm:
    mov w0, #-9
    VM_DIR_RETURN
Lvm_dir_bad_arm:
    mov w0, #-22
    VM_DIR_RETURN
Lvm_dir_io_arm:
    mov w0, #-5
    VM_DIR_RETURN

#elif defined(__x86_64__)
.globl VM_DIR_SYM(vpp_native_os_identity)
VM_DIR_SYM(vpp_native_os_identity):
    cmpl $5, %edi
    ja Lvm_identity_bad_x86
#if defined(__APPLE__)
    movl $0x2000014, %eax
    testl %edi, %edi
    jz 1f
    movl $0x2000027, %eax
    cmpl $1, %edi
    je 1f
    movl $0x2000018, %eax     // getuid
    cmpl $2, %edi
    je 1f
    movl $0x2000019, %eax     // geteuid
    cmpl $3, %edi
    je 1f
    movl $0x200002f, %eax     // getgid
    cmpl $4, %edi
    je 1f
    movl $0x200002b, %eax     // getegid
1:  syscall
    jnc 2f
    negl %eax
#else
    movl $39, %eax
    testl %edi, %edi
    jz 1f
    movl $110, %eax
    cmpl $1, %edi
    je 1f
    movl $102, %eax          // getuid
    cmpl $2, %edi
    je 1f
    movl $107, %eax          // geteuid
    cmpl $3, %edi
    je 1f
    movl $104, %eax          // getgid
    cmpl $4, %edi
    je 1f
    movl $108, %eax          // getegid
1:  syscall
#endif
2:  ret
Lvm_identity_bad_x86:
    movl $-22, %eax
    ret

.macro VM_DIR_ENTER
    pushq %rbp
    movq %rsp, %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $32, %rsp
    movl %edi, -36(%rbp)
    movl %esi, -40(%rbp)
    movl %edx, -44(%rbp)
    call VM_DIR_SYM(vpp_native_memory_base)
    movq %rax, %r12
    testq %r12, %r12
    jz Lvm_dir_bad_x86
    movl -36(%rbp), %r15d
    movl -40(%rbp), %r13d
    movl -44(%rbp), %r14d
.endm
.macro VM_DIR_RETURN
    leaq -32(%rbp), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    leave
    ret
.endm
.globl VM_DIR_SYM(vpp_native_os_directory)
VM_DIR_SYM(vpp_native_os_directory):
    VM_DIR_ENTER
    cmpl $4, %r15d
    ja Lvm_dir_bad_x86
    testl %r15d, %r15d
    jz Lvm_dir_path_x86
    cmpl $3, %r15d
    jae Lvm_dir_path_x86
    cmpl $68162544, %r13d
    ja Lvm_dir_bad_x86
    testl $7, %r13d
    jnz Lvm_dir_bad_x86
    addq %r12, %r13
    cmpl $0x44524944, 8(%r13)
    jne Lvm_dir_closed_x86
    cmpl $2, %r15d
    je Lvm_dir_close_x86
    cmpl $68154368, %r14d
    ja Lvm_dir_bad_x86
    addq %r12, %r14
#if defined(__APPLE__)
    call VM_DIR_LIB(__error)
    movl $0, (%rax)
    movq (%r13), %rdi
    call VM_DIR_LIB(readdir)
    testq %rax, %rax
    jz Lvm_dir_errno_x86
    movzwl 16(%rax), %r15d
    cmpl $22, %r15d
    jb Lvm_dir_io_x86
    cmpl $1048, %r15d
    ja Lvm_dir_io_x86
    movq %rax, %rsi
    movq %r14, %rdi
    movl %r15d, %edx
    call VM_DIR_LIB(memcpy)
    movl %r15d, %eax
#else
    movq (%r13), %rdi
    movq %r14, %rsi
    movl $8192, %edx
    movl $217, %eax
    syscall
#endif
    VM_DIR_RETURN
Lvm_dir_close_x86:
    movq (%r13), %rdi
    movl $0, 8(%r13)
    movq $0, (%r13)
#if defined(__APPLE__)
    call VM_DIR_LIB(closedir)
    testl %eax, %eax
    jnz Lvm_dir_errno_x86
#else
    movl $3, %eax
    syscall
#endif
    VM_DIR_RETURN
Lvm_dir_path_x86:
    cmpl $68158464, %r13d
    ja Lvm_dir_bad_x86
    addq %r12, %r13
    cmpl $4, %r15d
    je Lvm_dir_mkdir_x86
    cmpl $3, %r15d
    je Lvm_dir_remove_x86
    cmpl $68162544, %r14d
    ja Lvm_dir_bad_x86
    testl $7, %r14d
    jnz Lvm_dir_bad_x86
    addq %r12, %r14
    movl $0, 8(%r14)
#if defined(__APPLE__)
    movq %r13, %rdi
    xorl %esi, %esi
    movl $4096, %edx
    call VM_DIR_LIB(memchr)
    testq %rax, %rax
    jz Lvm_dir_bad_x86
    movq %r13, %rdi
    call VM_DIR_LIB(opendir)
    testq %rax, %rax
    jz Lvm_dir_errno_x86
#else
    movl $-100, %edi
    movq %r13, %rsi
    movl $0x90000, %edx
    xorl %r10d, %r10d
    movl $257, %eax
    syscall
    testq %rax, %rax
    js Lvm_dir_return_x86
#endif
    movq %rax, (%r14)
    movl $0x44524944, 8(%r14)
    xorl %eax, %eax
Lvm_dir_return_x86:
    VM_DIR_RETURN
Lvm_dir_remove_x86:
#if defined(__APPLE__)
    movq %r13, %rdi
    movl $0x2000089, %eax
    syscall
    jnc Lvm_dir_return_x86
    negl %eax
#else
    movl $-100, %edi
    movq %r13, %rsi
    movl $512, %edx
    movl $263, %eax
    syscall
#endif
    VM_DIR_RETURN
Lvm_dir_mkdir_x86:
#if defined(__APPLE__)
    movq %r13, %rdi
    movl $511, %esi
    movl $0x2000088, %eax
    syscall
    jnc Lvm_dir_return_x86
    negl %eax
#else
    movl $-100, %edi
    movq %r13, %rsi
    movl $511, %edx
    movl $258, %eax
    syscall
#endif
    VM_DIR_RETURN
#if defined(__APPLE__)
Lvm_dir_errno_x86:
    call VM_DIR_LIB(__error)
    movl (%rax), %eax
    negl %eax
    VM_DIR_RETURN
#endif
Lvm_dir_closed_x86:
    movl $-9, %eax
    VM_DIR_RETURN
Lvm_dir_bad_x86:
    movl $-22, %eax
    VM_DIR_RETURN
Lvm_dir_io_x86:
    movl $-5, %eax
    VM_DIR_RETURN
#endif
#undef VM_DIR_SYM
#undef VM_DIR_LIB
