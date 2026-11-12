// Kiểm tra chỉ số, lấy độ dài và đọc byte đối số chương trình.

.globl vpp_native_argc
vpp_native_argc:
    subq $56, %rsp
    call GetCommandLineW
    movq %rax, %rcx
    leaq 40(%rsp), %rdx
    call CommandLineToArgvW
    testq %rax, %rax
    je 1f
    movq %rax, %rcx
    call LocalFree
    movl 40(%rsp), %eax
    addq $56, %rsp
    ret
1:  movl $-1, %eax
    addq $56, %rsp
    ret
// Đọc đối số từ dòng lệnh UTF-16 của Win32 để giữ tên Unicode; __p___argv của MSVCRT đi qua ANSI có
// thể làm hỏng đường tiếng Việt. Lưu đệm đối số vừa đọc cho các lần truy vấn byte tiếp theo.
Ldoi_so_con_tro:
    pushq %rbp
    movq %rsp, %rbp
    subq $128, %rsp
    movl %ecx, -4(%rbp)
    cmpl $0, %ecx
    jl Ldoi_so_loi
    movq vpp_native_argument_buffer(%rip), %rax
    testq %rax, %rax
    je Ldoi_so_phan_giai
    cmpl vpp_native_argument_index(%rip), %ecx
    je Ldoi_so_tra_ve
Ldoi_so_phan_giai:
    call GetCommandLineW
    movq %rax, %rcx
    leaq -16(%rbp), %rdx
    call CommandLineToArgvW
    testq %rax, %rax
    je Ldoi_so_loi
    movq %rax, -24(%rbp)
    movl -4(%rbp), %ecx
    cmpl -16(%rbp), %ecx
    jae Ldoi_so_thu_hoi_loi
    movq (%rax,%rcx,8), %r8
    movq %r8, -32(%rbp)
// Lần gọi đầu lấy số byte cần thiết kể cả NUL kết thúc.
    movl $65001, %ecx
    xorl %edx, %edx
    movl $-1, %r9d
    movq $0, 32(%rsp)
    movl $0, 40(%rsp)
    movq $0, 48(%rsp)
    movq $0, 56(%rsp)
    call WideCharToMultiByte
    testl %eax, %eax
    jle Ldoi_so_thu_hoi_loi
    cmpl $1048576, %eax
    ja Ldoi_so_thu_hoi_loi
    movl %eax, -36(%rbp)
    movl $-1, vpp_native_argument_index(%rip)
    movq vpp_native_argument_buffer(%rip), %rcx
    movl %eax, %edx
    call realloc
    testq %rax, %rax
    je Ldoi_so_thu_hoi_loi
    movq %rax, vpp_native_argument_buffer(%rip)
    movl $65001, %ecx
    xorl %edx, %edx
    movq -32(%rbp), %r8
    movl $-1, %r9d
    movq %rax, 32(%rsp)
    movl -36(%rbp), %eax
    movl %eax, 40(%rsp)
    movq $0, 48(%rsp)
    movq $0, 56(%rsp)
    call WideCharToMultiByte
    cmpl -36(%rbp), %eax
    jne Ldoi_so_thu_hoi_loi
    movl -4(%rbp), %ecx
    movl %ecx, vpp_native_argument_index(%rip)
    movq -24(%rbp), %rcx
    call LocalFree
Ldoi_so_tra_ve:
    movq vpp_native_argument_buffer(%rip), %rax
    leave
    ret
Ldoi_so_thu_hoi_loi:
    movq -24(%rbp), %rcx
    call LocalFree
Ldoi_so_loi:
    xorl %eax, %eax
    leave
    ret
.globl vpp_native_os_argument_length
vpp_native_os_argument_length:
    subq $40, %rsp
    call Ldoi_so_con_tro
    testq %rax, %rax
    je 2f
    xorl %edx, %edx
1:  cmpl $1048576, %edx
    ja 2f
    cmpb $0, (%rax,%rdx)
    je 3f
    incl %edx
    jmp 1b
3:  movl %edx, %eax
    addq $40, %rsp
    ret
2:  movl $-1, %eax
    addq $40, %rsp
    ret
.globl vpp_native_os_argument_byte
vpp_native_os_argument_byte:
    pushq %rbp
    movq %rsp, %rbp
    subq $48, %rsp
    movl %edx, -4(%rbp)
    call Ldoi_so_con_tro
    testq %rax, %rax
    je 2f
    movl -4(%rbp), %ecx
    cmpl $1048576, %ecx
    jae 2f
    xorl %edx, %edx
1:  cmpb $0, (%rax,%rdx)
    je 2f
    cmpl %ecx, %edx
    je 3f
    incl %edx
    jmp 1b
3:  movzbl (%rax,%rdx), %eax
    jmp 4f
2:  movl $-1, %eax
4:  leave
    ret
