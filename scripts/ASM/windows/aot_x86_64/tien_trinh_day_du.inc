// Process control: public offsets are shared with tiến trình máy.vi; native
// handles live in the private tail (+64 onward) and are never truncated.
.text
.globl vpp_native_os_process
vpp_native_os_process:
    WIN_VAO
    cmpl $4, %r12d
    ja .Lwin_khong_hop_le
    cmpl $(VPP_ARENA_SIZE-1024), %r13d
    ja .Lwin_khong_hop_le
    testl $15, %r13d
    jnz .Lwin_khong_hop_le
    leaq (%rbx,%r13), %r15
    testl %r12d, %r12d
    jz .Lwin_tien_trinh_tao
    cmpl $1, %r12d
    je .Lwin_tien_trinh_tham_do
    cmpl $2, %r12d
    je .Lwin_tien_trinh_doc
    cmpl $3, %r12d
    je .Lwin_tien_trinh_doi
    jmp .Lwin_tien_trinh_dong
.Lwin_tien_trinh_tao:
    movl $0, 48(%r15)
    movl 20(%r15), %eax
    decl %eax
    cmpl $127, %eax
    ja .Lwin_khong_hop_le
    cmpl $128, 44(%r15)
    ja .Lwin_khong_hop_le
    movl 12(%r15), %eax
    cmpl $(VPP_ARENA_SIZE-512), %eax
    ja .Lwin_khong_hop_le
    movl $1, %ecx
    movl $2300000, %edx
    call calloc
    testq %rax, %rax
    jz .Lwin_khong_hop_le
    movq %rax, %rsi
    movq %rax, 112(%r15)
    // Layout: command[65536], argument[8192], overrides[128*8192],
    // followed by a bounded Unicode environment block.
    xorl %r14d, %r14d
    movq %rsi, %rdi
.Lwin_tien_trinh_doi_so:
    cmpl 20(%r15), %r14d
    jae .Lwin_tien_trinh_moi_truong
    movl 12(%r15), %eax
    leaq (%rbx,%rax), %rcx
    movl (%rcx,%r14,4), %eax
    cmpl $(VPP_ARENA_SIZE-4096), %eax
    ja .Lwin_tien_trinh_tao_loi
    leaq (%rbx,%rax), %rcx
    leaq 65536(%rsi), %rdx
    call Lduong_utf8_sang_utf16
    testl %eax, %eax
    jz .Lwin_tien_trinh_tao_loi
    // A quoted argument requires at most twice its UTF-16 width plus 3.
    leaq (%rdi,%rax,4), %rax
    leaq 65528(%rsi), %rdx
    cmpq %rdx, %rax
    ja .Lwin_tien_trinh_tao_loi
    leaq 65536(%rsi), %rcx
    movq %rdi, %rdx
    call Ltao_tien_trinh_trich_utf16
    leaq 2(%rdx), %rdi
    movw $32, (%rdi)
    addq $2, %rdi
    incl %r14d
    jmp .Lwin_tien_trinh_doi_so
.Lwin_tien_trinh_moi_truong:
    movw $0, -2(%rdi)
    xorl %r14d, %r14d
.Lwin_tien_trinh_bien:
    cmpl 44(%r15), %r14d
    jae .Lwin_tien_trinh_ke_thua
    movl 24(%r15), %eax
    cmpl $(VPP_ARENA_SIZE-512), %eax
    ja .Lwin_tien_trinh_tao_loi
    leaq (%rbx,%rax), %rcx
    movl (%rcx,%r14,4), %eax
    cmpl $(VPP_ARENA_SIZE-4096), %eax
    ja .Lwin_tien_trinh_tao_loi
    leaq (%rbx,%rax), %rcx
    movl %r14d, %eax
    shll $13, %eax
    leaq 73728(%rsi,%rax), %rdx
    call Lduong_utf8_sang_utf16
    testl %eax, %eax
    jz .Lwin_tien_trinh_tao_loi
    incl %r14d
    jmp .Lwin_tien_trinh_bien
.Lwin_tien_trinh_ke_thua:
    call GetEnvironmentStringsW
    testq %rax, %rax
    jz .Lwin_tien_trinh_tao_loi
    movq %rax, 120(%r15)
    movq %rax, %r12
    leaq 1122304(%rsi), %rdi
    movq %rdi, 128(%r15)
.Lwin_tien_trinh_ke_thua_muc:
    cmpw $0, (%r12)
    je .Lwin_tien_trinh_them_bien
    xorl %r14d, %r14d
.Lwin_tien_trinh_so_bien:
    cmpl 44(%r15), %r14d
    jae .Lwin_tien_trinh_chep_bien
    movl %r14d, %eax
    shll $13, %eax
    leaq 73728(%rsi,%rax), %rdx
    xorq %r8, %r8
1:  cmpw $0, (%rdx,%r8,2)
    je .Lwin_tien_trinh_tao_loi
    cmpw $61, (%rdx,%r8,2)
    je 2f
    incq %r8
    jmp 1b
2:  incq %r8
    movq %r12, %rcx
    call _wcsnicmp
    testl %eax, %eax
    jz .Lwin_tien_trinh_bo_bien
    incl %r14d
    jmp .Lwin_tien_trinh_so_bien
.Lwin_tien_trinh_chep_bien:
    movq %r12, %rcx
    call .Lwin_tien_trinh_chep_utf16
    testl %eax, %eax
    js .Lwin_tien_trinh_tao_loi
.Lwin_tien_trinh_bo_bien:
    movq %r12, %rcx
    call wcslen
    leaq 2(%r12,%rax,2), %r12
    jmp .Lwin_tien_trinh_ke_thua_muc
.Lwin_tien_trinh_them_bien:
    movq 120(%r15), %rcx
    call FreeEnvironmentStringsW
    movq $0, 120(%r15)
    xorl %r14d, %r14d
1:  cmpl 44(%r15), %r14d
    jae .Lwin_tien_trinh_ong
    movl %r14d, %eax
    shll $13, %eax
    leaq 73728(%rsi,%rax), %rcx
    call .Lwin_tien_trinh_chep_utf16
    testl %eax, %eax
    js .Lwin_tien_trinh_tao_loi
    incl %r14d
    jmp 1b
.Lwin_tien_trinh_ong:
    movw $0, (%rdi)
    movl $24, 136(%r15)
    movq $0, 144(%r15)
    movl $1, 152(%r15)
    leaq 64(%r15), %rcx
    leaq 72(%r15), %rdx
    leaq 136(%r15), %r8
    xorl %r9d, %r9d
    call CreatePipe
    testl %eax, %eax
    jz .Lwin_tien_trinh_tao_loi
    movq 64(%r15), %rcx
    movl $1, %edx
    xorl %r8d, %r8d
    call SetHandleInformation
    testl %eax, %eax
    jz .Lwin_tien_trinh_tao_loi
    leaq 80(%r15), %rcx
    leaq 88(%r15), %rdx
    leaq 136(%r15), %r8
    xorl %r9d, %r9d
    call CreatePipe
    testl %eax, %eax
    jz .Lwin_tien_trinh_tao_loi
    movq 80(%r15), %rcx
    movl $1, %edx
    xorl %r8d, %r8d
    call SetHandleInformation
    testl %eax, %eax
    jz .Lwin_tien_trinh_tao_loi
    movl $104, 160(%r15)
    movl $0x100, 220(%r15)
    movl $-10, %ecx
    call GetStdHandle
    movq %rax, 240(%r15)
    movq 72(%r15), %rax
    movq %rax, 248(%r15)
    movq 88(%r15), %rax
    movq %rax, 256(%r15)
    xorl %ecx, %ecx
    movq %rsi, %rdx
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    movq $1, 32(%rsp)
    movq $0x400, 40(%rsp)
    movq 128(%r15), %rax
    movq %rax, 48(%rsp)
    movq $0, 56(%rsp)
    leaq 160(%r15), %rax
    movq %rax, 64(%rsp)
    leaq 272(%r15), %rax
    movq %rax, 72(%rsp)
    call CreateProcessW
    testl %eax, %eax
    jz .Lwin_tien_trinh_tao_loi
    movq 280(%r15), %rcx
    call CloseHandle
    movq $0, 280(%r15)
    movq 72(%r15), %rcx
    call CloseHandle
    movq $0, 72(%r15)
    movq 88(%r15), %rcx
    call CloseHandle
    movq $0, 88(%r15)
    movq %rsi, %rcx
    call free
    movq $0, 112(%r15)
    movl 288(%r15), %eax
    movl %eax, (%r15)
    movl $1, 4(%r15)
    movl $2, 8(%r15)
    movl $1, %eax
    WIN_RA
.Lwin_tien_trinh_chep_utf16:
    // rcx is the input, rdi the output cursor; bound the entire block.
    movq %rcx, %rdx
1:  leaq 2299996(%rsi), %rax
    cmpq %rax, %rdi
    jae 2f
    movw (%rdx), %ax
    movw %ax, (%rdi)
    addq $2, %rdx
    addq $2, %rdi
    testw %ax, %ax
    jnz 1b
    xorl %eax, %eax
    ret
2:  movl $-7, %eax
    ret
.Lwin_tien_trinh_tao_loi:
    call .Lwin_loi
    negl %eax
    movl %eax, 48(%r15)
    movq 120(%r15), %rcx
    testq %rcx, %rcx
    jz 1f
    call FreeEnvironmentStringsW
1:  movq 112(%r15), %rcx
    call free
    movq $0, 112(%r15)
    movq $0, 120(%r15)
    call .Lwin_tien_trinh_dong_ong
    xorl %eax, %eax
    WIN_RA
.Lwin_tien_trinh_tham_do:
    movl 28(%r15), %r14d
    cmpl $5000, %r14d
    ja .Lwin_khong_hop_le
    call GetTickCount64
    addq %r14, %rax
    movq %rax, -72(%rbp)
1:  xorl %r12d, %r12d
    movl $1, %r14d
    call .Lwin_tien_trinh_san_sang
    orl %eax, %r12d
    movl $2, %r14d
    call .Lwin_tien_trinh_san_sang
    shll $1, %eax
    orl %eax, %r12d
    testl %r12d, %r12d
    jnz 3f
    cmpl $0, 4(%r15)
    jge 2f
    cmpl $0, 8(%r15)
    jge 2f
    movl $4, %r12d
    jmp 3f
2:  call GetTickCount64
    cmpq -72(%rbp), %rax
    jae 3f
    movl $1, %ecx
    call Sleep
    jmp 1b
3:  movl %r12d, %eax
    WIN_RA
.Lwin_tien_trinh_san_sang:
    subq $56, %rsp
    cmpl $0, (%r15,%r14,4)
    jl 2f
    movl %r14d, %eax
    shll $4, %eax
    movq 48(%r15,%rax), %rcx
    xorl %edx, %edx
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    leaq 48(%rsp), %rax
    movq %rax, 32(%rsp)
    movq $0, 40(%rsp)
    call PeekNamedPipe
    testl %eax, %eax
    jz 1f
    cmpl $0, 48(%rsp)
    setne %al
    movzbl %al, %eax
    jmp 3f
1:  movl $1, %eax
    jmp 3f
2:  xorl %eax, %eax
3:  addq $56, %rsp
    ret
.Lwin_tien_trinh_doc:
    movl 40(%r15), %r14d
    cmpl $1, %r14d
    jb .Lwin_khong_hop_le
    cmpl $2, %r14d
    ja .Lwin_khong_hop_le
    cmpl $0, (%r15,%r14,4)
    jl .Lwin_tien_trinh_doc_fin
    movl 32(%r15), %eax
    cmpl $(VPP_ARENA_SIZE-4096), %eax
    ja .Lwin_khong_hop_le
    cmpl $4096, 36(%r15)
    ja .Lwin_khong_hop_le
    leaq (%rbx,%rax), %rdx
    movl %r14d, %eax
    shll $4, %eax
    movq 48(%r15,%rax), %rcx
    movl 36(%r15), %r8d
    leaq -72(%rbp), %r9
    movq $0, 32(%rsp)
    call ReadFile
    testl %eax, %eax
    jz .Lwin_tien_trinh_doc_dong
    movl -72(%rbp), %eax
    testl %eax, %eax
    jnz .Lwin_tien_trinh_doc_xong
.Lwin_tien_trinh_doc_dong:
    movl %r14d, %eax
    shll $4, %eax
    leaq 48(%r15,%rax), %r13
    movq (%r13), %rcx
    call CloseHandle
    movq $0, (%r13)
    movl $-1, (%r15,%r14,4)
.Lwin_tien_trinh_doc_fin:
    xorl %eax, %eax
.Lwin_tien_trinh_doc_xong:
    WIN_RA
.Lwin_tien_trinh_doi:
    movq 272(%r15), %rcx
    testq %rcx, %rcx
    jz .Lwin_khong_hop_le
    movl $-1, %edx
    call WaitForSingleObject
    movq 272(%r15), %rcx
    leaq -72(%rbp), %rdx
    call GetExitCodeProcess
    testl %eax, %eax
    jz .Lwin_khong_hop_le
    movq 272(%r15), %rcx
    call CloseHandle
    movq $0, 272(%r15)
    movl $0, (%r15)
    movl -72(%rbp), %eax
    WIN_RA
.Lwin_tien_trinh_dong:
    call .Lwin_tien_trinh_dong_ong
    movq 272(%r15), %rcx
    testq %rcx, %rcx
    jz 1f
    movl $1, %edx
    call TerminateProcess
    movq 272(%r15), %rcx
    movl $-1, %edx
    call WaitForSingleObject
    movq 272(%r15), %rcx
    call CloseHandle
    movq $0, 272(%r15)
1:  movl $0, (%r15)
    movl $-1, 4(%r15)
    movl $-1, 8(%r15)
    xorl %eax, %eax
    WIN_RA
.Lwin_tien_trinh_dong_ong:
    subq $40, %rsp
    movq $64, -80(%rbp)
1:  movq -80(%rbp), %rax
    movq (%r15,%rax), %rcx
    testq %rcx, %rcx
    jz 2f
    movq $0, (%r15,%rax)
    call CloseHandle
2:  addq $8, -80(%rbp)
    cmpq $96, -80(%rbp)
    jb 1b
    addq $40, %rsp
    ret
