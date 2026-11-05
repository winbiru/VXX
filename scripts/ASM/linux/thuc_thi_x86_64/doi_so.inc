// Kiểm tra chỉ số, lấy độ dài và đọc byte đối số chương trình.

// _start Linux giữ nguyên các con trỏ argc/argv ban đầu từ nhân.
.globl vpp_native_argc
vpp_native_argc:
    movl vpp_linux_argc(%rip), %eax
    ret
.Ldoi_so_con_tro:
    testl %edi, %edi
    js .Ldoi_so_thieu
    cmpl vpp_linux_argc(%rip), %edi
    jae .Ldoi_so_thieu
    movq vpp_linux_argv(%rip), %rax
    testq %rax, %rax
    je .Ldoi_so_thieu
    movq (%rax,%rdi,8), %rax
    ret
.Ldoi_so_thieu:
    xorl %eax, %eax
    ret
.globl vpp_native_os_argument_length
vpp_native_os_argument_length:
    subq $8, %rsp
    call .Ldoi_so_con_tro
    addq $8, %rsp
    testq %rax, %rax
    je .Ldoi_so_sai_do_dai
    xorl %ecx, %ecx
.Ldoi_so_quet_do_dai:
    cmpl $1048576, %ecx
    jae .Ldoi_so_sai_do_dai
    cmpb $0, (%rax,%rcx)
    je .Ldoi_so_do_dai_xong
    incl %ecx
    jmp .Ldoi_so_quet_do_dai
.Ldoi_so_do_dai_xong:
    movl %ecx, %eax
    ret
.Ldoi_so_sai_do_dai:
    movl $-1, %eax
    ret
.globl vpp_native_os_argument_byte
vpp_native_os_argument_byte:
    pushq %rbx
    movl %esi, %ebx
    call .Ldoi_so_con_tro
    testq %rax, %rax
    je .Ldoi_so_sai_byte
    cmpl $1048576, %ebx
    jae .Ldoi_so_sai_byte
    xorl %edx, %edx
.Ldoi_so_quet_byte:
    cmpb $0, (%rax,%rdx)
    je .Ldoi_so_sai_byte
    cmpl %ebx, %edx
    je .Ldoi_so_byte_xong
    incl %edx
    jmp .Ldoi_so_quet_byte
.Ldoi_so_byte_xong:
    movzbl (%rax,%rdx), %eax
    popq %rbx
    ret
.Ldoi_so_sai_byte:
    movl $-1, %eax
    popq %rbx
    ret

