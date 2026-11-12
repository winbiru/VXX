// Tạo tệp tạm độc quyền với đuôi ngẫu nhiên.

// Tạo tệp tạm bằng openat(O_CREAT|O_EXCL) và getrandom. Bên gọi sở hữu đường dẫn trong vùng nhớ, kết
// thúc bằng sáu ký tự X.
.globl vpp_native_os_mkstemp
vpp_native_os_mkstemp:
    VPP_VAO_BO_NHO
    VPP_KIEM_TRA_DUONG %edi
    cmpl $6, %ecx
    jb .Lbo_nho_khong_hop_le
    leaq (%r11,%rdi), %r8
    movq %r8, -32(%rbp)          // Đường dẫn đầy đủ
    addq %rcx, %r8
    subq $6, %r8
    movq %r8, -24(%rbp)          // Đuôi XXXXXX có thể ghi
    movl $6, %ecx
1:  cmpb $88, (%r8)             // X
    jne .Lbo_nho_khong_hop_le
    incq %r8
    decl %ecx
    jne 1b
    movl $64, -16(%rbp)         // Chỉ thử lại khi trùng tên EEXIST
.Ltao_thu_lai:
    leaq -40(%rbp), %rdi
    movl $8, %esi
    xorl %edx, %edx
    call vpp_sys_getrandom
    cmpq $-4, %rax              // EINTR
    je .Ltao_thu_lai
    cmpq $8, %rax
    je .Ltao_ngau_nhien_san_sang
    cmpq $-4095, %rax
    jae .Ltao_ket_qua
    movq $-5, %rax              // getrandom trả thiếu byte: EIO
    jmp .Ltao_ket_qua
.Ltao_ngau_nhien_san_sang:
    movq -40(%rbp), %rax
    movq -24(%rbp), %r8
    leaq .Ltao_bang_chu(%rip), %r9
    movl $62, %r10d
    movl $6, %ecx
.Ltao_lap_day:
    xorl %edx, %edx
    divq %r10
    movb (%r9,%rdx), %dl
    movb %dl, (%r8)
    incq %r8
    decl %ecx
    jne .Ltao_lap_day
    movq $-100, %rdi            // AT_FDCWD
    movq -32(%rbp), %rsi
    movl $194, %edx             // O_RDWR | O_CREAT | O_EXCL
    movl $384, %ecx             // 0600
    call vpp_sys_openat
    cmpq $-17, %rax             // EEXIST
    jne .Ltao_ket_qua
    decl -16(%rbp)
    jne .Ltao_thu_lai
.Ltao_ket_qua:
    VPP_TRA_SYSCALL

