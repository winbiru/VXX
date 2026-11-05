// ABI i32: identity(loại), directory(lệnh, điều khiển, dữ liệu). Lệnh: 0 mở(đường, ô16), 1 đọc(ô16,
// đệm8192), 2 đóng(ô16), 3 xóa thư mục(đường), 4 tạo thư mục(đường, 0777). Con trỏ DIR giữ tại đây;
// V++ giải mã bản ghi.
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
    b.hi Lvm_dinh_danh_sai_bat
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
Lvm_dinh_danh_sai_bat:
    mov w0, #-22
    ret

.macro VM_VAO_THU_MUC
    stp x19, x20, [sp, #-80]!
    stp x21, x22, [sp, #16]
    stp x23, x30, [sp, #32]
    mov w19, w0
    mov w20, w1
    mov w22, w2
    bl VM_DIR_SYM(vpp_native_memory_base)
    mov x21, x0
    cbz x21, Lvm_thu_muc_sai_bat
.endm
.macro VM_TRA_THU_MUC
    ldp x23, x30, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #80
    ret
.endm
.globl VM_DIR_SYM(vpp_native_os_directory)
VM_DIR_SYM(vpp_native_os_directory):
    VM_VAO_THU_MUC
    cmp w19, #4
    b.hi Lvm_thu_muc_sai_bat
    cbz w19, Lvm_thu_muc_duong_bat
    cmp w19, #3
    b.hs Lvm_thu_muc_duong_bat
// Khối điều khiển giữ mã tài nguyên OS đủ 64 bit và cờ còn hoạt động.
    tbnz w20, #31, Lvm_thu_muc_sai_bat
    tst w20, #7
    b.ne Lvm_thu_muc_sai_bat
    mov w10, #0x13f0
    movk w10, #0x410, lsl #16
    cmp w20, w10
    b.hi Lvm_thu_muc_sai_bat
    add x23, x21, w20, uxtw
    ldr w10, [x23, #8]
    mov w11, #0x4944
    movk w11, #0x4452, lsl #16
    cmp w10, w11
    b.ne Lvm_thu_muc_da_dong_bat
    cmp w19, #2
    b.eq Lvm_thu_muc_dong_bat
    tbnz w22, #31, Lvm_thu_muc_sai_bat
    mov w10, #0xf400           // Kích thước vùng nhớ trừ 8192 = 0x40ff400
    movk w10, #0x40f, lsl #16
    cmp w22, w10
    b.hi Lvm_thu_muc_sai_bat
    add x22, x21, w22, uxtw
#if defined(__APPLE__)
    bl ___error
    str wzr, [x0]
    ldr x0, [x23]
    bl _readdir
    cbz x0, Lvm_thu_muc_errno_bat // errno=0 phân biệt hết tệp với lỗi
    ldrh w19, [x0, #16]
    cmp w19, #22
    b.lo Lvm_thu_muc_nhap_xuat_bat
    cmp w19, #1048
    b.hi Lvm_thu_muc_nhap_xuat_bat
    mov x1, x0
    mov x0, x22
    uxtw x2, w19
    bl _memcpy                // Chép byte dirent, V++ giải mã
    mov w0, w19
#else
    ldr x0, [x23]
    mov x1, x22
    mov x2, #8192
    mov x8, #61               // getdents64
    svc #0
#endif
    VM_TRA_THU_MUC
Lvm_thu_muc_dong_bat:
    ldr x0, [x23]
    str wzr, [x23, #8]       // Thu hồi ngay cả khi đóng tài nguyên OS thất bại
    str xzr, [x23]
#if defined(__APPLE__)
    bl _closedir
    cbnz w0, Lvm_thu_muc_errno_bat
#else
    mov x8, #57
    svc #0
#endif
    VM_TRA_THU_MUC
Lvm_thu_muc_duong_bat:
    tbnz w20, #31, Lvm_thu_muc_sai_bat
    mov w10, #0x400
    movk w10, #0x410, lsl #16 // Kích thước vùng nhớ trừ 4096
    cmp w20, w10
    b.hi Lvm_thu_muc_sai_bat
    add x20, x21, w20, uxtw
    cmp w19, #4
    b.eq Lvm_thu_muc_tao_thu_muc_bat
    cmp w19, #3
    b.eq Lvm_thu_muc_xoa_bat
    tbnz w22, #31, Lvm_thu_muc_sai_bat
    tst w22, #7
    b.ne Lvm_thu_muc_sai_bat
    mov w10, #0x13f0
    movk w10, #0x410, lsl #16
    cmp w22, w10
    b.hi Lvm_thu_muc_sai_bat
    add x23, x21, w22, uxtw
    str wzr, [x23, #8]
#if defined(__APPLE__)
    mov x0, x20
    mov w1, #0
    mov x2, #4096
    bl _memchr
    cbz x0, Lvm_thu_muc_sai_bat
    mov x0, x20
    bl _opendir
    cbz x0, Lvm_thu_muc_errno_bat
#else
    mov x0, #-100
    mov x1, x20
    mov x2, #0x90000          // O_DIRECTORY | O_CLOEXEC
    mov x3, #0
    mov x8, #56               // openat
    svc #0
    tbnz x0, #63, Lvm_thu_muc_tra_ve_bat
#endif
    str x0, [x23]
    mov w10, #0x4944
    movk w10, #0x4452, lsl #16
    str w10, [x23, #8]
    mov w0, #0
Lvm_thu_muc_tra_ve_bat:
    VM_TRA_THU_MUC
Lvm_thu_muc_xoa_bat:
#if defined(__APPLE__)
    mov x0, x20
    mov x16, #137             // rmdir
    svc #0x80
    b.cc Lvm_thu_muc_tra_ve_bat
    neg w0, w0
#else
    mov x0, #-100
    mov x1, x20
    mov x2, #512              // AT_REMOVEDIR
    mov x8, #35               // unlinkat
    svc #0
#endif
    VM_TRA_THU_MUC
Lvm_thu_muc_tao_thu_muc_bat:
#if defined(__APPLE__)
    mov x0, x20
    mov x1, #511
    mov x16, #136             // mkdir với quyền 0777, chịu ảnh hưởng umask
    svc #0x80
    b.cc Lvm_thu_muc_tra_ve_bat
    neg w0, w0
#else
    mov x0, #-100
    mov x1, x20
    mov x2, #511
    mov x8, #34               // mkdirat
    svc #0
#endif
    VM_TRA_THU_MUC
#if defined(__APPLE__)
Lvm_thu_muc_errno_bat:
    bl ___error
    ldr w0, [x0]
    neg w0, w0
    VM_TRA_THU_MUC
#endif
Lvm_thu_muc_da_dong_bat:
    mov w0, #-9
    VM_TRA_THU_MUC
Lvm_thu_muc_sai_bat:
    mov w0, #-22
    VM_TRA_THU_MUC
Lvm_thu_muc_nhap_xuat_bat:
    mov w0, #-5
    VM_TRA_THU_MUC

#elif defined(__x86_64__)
.globl VM_DIR_SYM(vpp_native_os_identity)
VM_DIR_SYM(vpp_native_os_identity):
    cmpl $5, %edi
    ja Lvm_dinh_danh_sai_x86
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
Lvm_dinh_danh_sai_x86:
    movl $-22, %eax
    ret

.macro VM_VAO_THU_MUC
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
    jz Lvm_thu_muc_sai_x86
    movl -36(%rbp), %r15d
    movl -40(%rbp), %r13d
    movl -44(%rbp), %r14d
.endm
.macro VM_TRA_THU_MUC
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
    VM_VAO_THU_MUC
    cmpl $4, %r15d
    ja Lvm_thu_muc_sai_x86
    testl %r15d, %r15d
    jz Lvm_thu_muc_duong_x86
    cmpl $3, %r15d
    jae Lvm_thu_muc_duong_x86
    cmpl $68162544, %r13d
    ja Lvm_thu_muc_sai_x86
    testl $7, %r13d
    jnz Lvm_thu_muc_sai_x86
    addq %r12, %r13
    cmpl $0x44524944, 8(%r13)
    jne Lvm_thu_muc_da_dong_x86
    cmpl $2, %r15d
    je Lvm_thu_muc_dong_x86
    cmpl $68154368, %r14d
    ja Lvm_thu_muc_sai_x86
    addq %r12, %r14
#if defined(__APPLE__)
    call VM_DIR_LIB(__error)
    movl $0, (%rax)
    movq (%r13), %rdi
    call VM_DIR_LIB(readdir)
    testq %rax, %rax
    jz Lvm_thu_muc_errno_x86
    movzwl 16(%rax), %r15d
    cmpl $22, %r15d
    jb Lvm_thu_muc_nhap_xuat_x86
    cmpl $1048, %r15d
    ja Lvm_thu_muc_nhap_xuat_x86
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
    VM_TRA_THU_MUC
Lvm_thu_muc_dong_x86:
    movq (%r13), %rdi
    movl $0, 8(%r13)
    movq $0, (%r13)
#if defined(__APPLE__)
    call VM_DIR_LIB(closedir)
    testl %eax, %eax
    jnz Lvm_thu_muc_errno_x86
#else
    movl $3, %eax
    syscall
#endif
    VM_TRA_THU_MUC
Lvm_thu_muc_duong_x86:
    cmpl $68158464, %r13d
    ja Lvm_thu_muc_sai_x86
    addq %r12, %r13
#if !defined(__APPLE__)
// Nhân Linux đọc đường dẫn kết thúc NUL. Kiểm tra đủ 4096 byte nằm trong vùng nhớ trước khi truyền
// con trỏ vào syscall.
    movq %r13, %rax
    movl $4096, %ecx
1:  cmpb $0, (%rax)
    je 2f
    incq %rax
    decl %ecx
    jnz 1b
    jmp Lvm_thu_muc_sai_x86
2:
#endif
    cmpl $4, %r15d
    je Lvm_thu_muc_tao_thu_muc_x86
    cmpl $3, %r15d
    je Lvm_thu_muc_xoa_x86
    cmpl $68162544, %r14d
    ja Lvm_thu_muc_sai_x86
    testl $7, %r14d
    jnz Lvm_thu_muc_sai_x86
    addq %r12, %r14
    movl $0, 8(%r14)
#if defined(__APPLE__)
    movq %r13, %rdi
    xorl %esi, %esi
    movl $4096, %edx
    call VM_DIR_LIB(memchr)
    testq %rax, %rax
    jz Lvm_thu_muc_sai_x86
    movq %r13, %rdi
    call VM_DIR_LIB(opendir)
    testq %rax, %rax
    jz Lvm_thu_muc_errno_x86
#else
    movl $-100, %edi
    movq %r13, %rsi
    movl $0x90000, %edx
    xorl %r10d, %r10d
    movl $257, %eax
    syscall
    testq %rax, %rax
    js Lvm_thu_muc_tra_ve_x86
#endif
    movq %rax, (%r14)
    movl $0x44524944, 8(%r14)
    xorl %eax, %eax
Lvm_thu_muc_tra_ve_x86:
    VM_TRA_THU_MUC
Lvm_thu_muc_xoa_x86:
#if defined(__APPLE__)
    movq %r13, %rdi
    movl $0x2000089, %eax
    syscall
    jnc Lvm_thu_muc_tra_ve_x86
    negl %eax
#else
    movl $-100, %edi
    movq %r13, %rsi
    movl $512, %edx
    movl $263, %eax
    syscall
#endif
    VM_TRA_THU_MUC
Lvm_thu_muc_tao_thu_muc_x86:
#if defined(__APPLE__)
    movq %r13, %rdi
    movl $511, %esi
    movl $0x2000088, %eax
    syscall
    jnc Lvm_thu_muc_tra_ve_x86
    negl %eax
#else
    movl $-100, %edi
    movq %r13, %rsi
    movl $511, %edx
    movl $258, %eax
    syscall
#endif
    VM_TRA_THU_MUC
#if defined(__APPLE__)
Lvm_thu_muc_errno_x86:
    call VM_DIR_LIB(__error)
    movl (%rax), %eax
    negl %eax
    VM_TRA_THU_MUC
#endif
Lvm_thu_muc_da_dong_x86:
    movl $-9, %eax
    VM_TRA_THU_MUC
Lvm_thu_muc_sai_x86:
    movl $-22, %eax
    VM_TRA_THU_MUC
Lvm_thu_muc_nhap_xuat_x86:
    movl $-5, %eax
    VM_TRA_THU_MUC
#endif
#undef VM_DIR_SYM
#undef VM_DIR_LIB
