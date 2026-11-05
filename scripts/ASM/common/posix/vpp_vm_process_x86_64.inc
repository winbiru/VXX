// Khối điều khiển tiến trình V++ trên SysV x86-64. Mọi địa chỉ khách đều là độ lệch đã kiểm tra. rbx
// giữ vùng nhớ, r12 giữ khối điều khiển, r13d giữ lệnh; ngăn xếp được căn chỉnh cho libc.
.macro PX_TRA_VE
    leaq 8(%rsp), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.endm
.macro PX_MA_LOI
#ifdef __APPLE__
    call VM_PROC_LIB(__error)
#else
    call VM_PROC_LIB(__errno_location)
#endif
    movl (%rax), %eax
.endm

.globl VM_PROC_SYM(vpp_native_os_process)
VM_PROC_SYM(vpp_native_os_process):
    pushq %rbp
    movq %rsp, %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $8, %rsp
    movl %edi, %r13d
    movl %esi, %r14d
    call VM_PROC_SYM(vpp_native_memory_base)
    movq %rax, %rbx
    testq %rbx, %rbx
    je Lpx_khong_hop_le
    cmpl $4, %r13d
    ja Lpx_khong_hop_le
    cmpl $(67000000-1024), %r14d
    ja Lpx_khong_hop_le
    testl $15, %r14d
    jnz Lpx_khong_hop_le
    leaq (%rbx,%r14), %r12
    testl %r13d, %r13d
    je Lpx_tao_tien_trinh
    cmpl $1, %r13d
    je Lpx_tham_do
    cmpl $2, %r13d
    je Lpx_doc
    cmpl $3, %r13d
    je Lpx_doi
    jmp Lpx_dong

Lpx_tao_tien_trinh:
    movl 20(%r12), %r15d
    cmpl $1, %r15d
    jl Lpx_khong_hop_le
    cmpl $128, %r15d
    jg Lpx_khong_hop_le
    movl 12(%r12), %eax
    testl $3, %eax
    jnz Lpx_khong_hop_le
    cmpl $(67000000-512), %eax
    ja Lpx_khong_hop_le
    movl 16(%r12), %ecx
    testl $7, %ecx
    jnz Lpx_khong_hop_le
    cmpl $(67000000-1032), %ecx
    ja Lpx_khong_hop_le
    xorl %r14d, %r14d
Lpx_argv:
    cmpl %r15d, %r14d
    jge Lpx_argv_xong
    movl 12(%r12), %eax
    leaq (%rbx,%rax), %r10
    movl (%r10,%r14,4), %ecx
    cmpl $8388624, %ecx
    jb Lpx_khong_hop_le
    cmpl $(67000000-4096), %ecx
    ja Lpx_khong_hop_le
    leaq (%rbx,%rcx), %rdx
    movl 16(%r12), %eax
    leaq (%rbx,%rax), %r8
    movq %rdx, (%r8,%r14,8)
    xorl %r9d, %r9d
Lpx_doi_so_byte:
    cmpb $0, (%rdx,%r9)
    je Lpx_doi_so_hop_le
    incl %r9d
    cmpl $4096, %r9d
    jb Lpx_doi_so_byte
    jmp Lpx_khong_hop_le
Lpx_doi_so_hop_le:
    incl %r14d
    jmp Lpx_argv
Lpx_argv_xong:
    movq $0, (%r8,%r14,8)
    movl $0, 48(%r12)
    movl $-1, 64(%r12)
    movl $-1, 68(%r12)
    movl $-1, 72(%r12)
    movl $-1, 76(%r12)
    movl $0, 88(%r12)
    movl $0, 92(%r12)
    movl $0, 96(%r12)
    leaq 64(%r12), %rdi
    call VM_PROC_LIB(pipe)
    testl %eax, %eax
    jnz Lpx_tao_tien_trinh_errno
    leaq 72(%r12), %rdi
    call VM_PROC_LIB(pipe)
    testl %eax, %eax
    jnz Lpx_tao_tien_trinh_errno
    xorl %r14d, %r14d
Lpx_mo_ta_doi_mo_ta:
    cmpl $4, %r14d
    jge Lpx_mo_ta_dong_khi_exec_khoi_tao
    movl 64(%r12,%r14,4), %edi
    cmpl $3, %edi
    jge Lpx_mo_ta_doi_mo_ta_tiep
#ifdef __APPLE__
    movl $67, %esi                 // F_DUPFD_CLOEXEC
#else
    movl $1030, %esi
#endif
    movl $3, %edx
    call VM_PROC_LIB(fcntl)
    testl %eax, %eax
    js Lpx_tao_tien_trinh_errno
    movl %eax, %r15d
    movl 64(%r12,%r14,4), %edi
    call VM_PROC_LIB(close)
    movl %r15d, 64(%r12,%r14,4)
Lpx_mo_ta_doi_mo_ta_tiep:
    incl %r14d
    jmp Lpx_mo_ta_doi_mo_ta
Lpx_mo_ta_dong_khi_exec_khoi_tao:
    xorl %r14d, %r14d
Lpx_mo_ta_dong_khi_exec:
    movl 64(%r12,%r14,4), %edi
    movl $2, %esi
    movl $1, %edx
    call VM_PROC_LIB(fcntl)
    testl %eax, %eax
    js Lpx_tao_tien_trinh_errno
    incl %r14d
    cmpl $4, %r14d
    jl Lpx_mo_ta_dong_khi_exec
    leaq 192(%r12), %rdi
    call VM_PROC_LIB(posix_spawn_file_actions_init)
    testl %eax, %eax
    jnz Lpx_tao_tien_trinh_loi
    movl $1, 88(%r12)
    leaq 192(%r12), %rdi
    movl 68(%r12), %esi
    movl $1, %edx
    call VM_PROC_LIB(posix_spawn_file_actions_adddup2)
    testl %eax, %eax
    jnz Lpx_tao_tien_trinh_loi
    leaq 192(%r12), %rdi
    movl 76(%r12), %esi
    movl $2, %edx
    call VM_PROC_LIB(posix_spawn_file_actions_adddup2)
    testl %eax, %eax
    jnz Lpx_tao_tien_trinh_loi
    xorl %r14d, %r14d
Lpx_mo_ta_them_dong:
    leaq 192(%r12), %rdi
    movl 64(%r12,%r14,4), %esi
    call VM_PROC_LIB(posix_spawn_file_actions_addclose)
    testl %eax, %eax
    jnz Lpx_tao_tien_trinh_loi
    incl %r14d
    cmpl $4, %r14d
    jl Lpx_mo_ta_them_dong
    leaq 320(%r12), %rdi
    call VM_PROC_LIB(posix_spawnattr_init)
    testl %eax, %eax
    jnz Lpx_tao_tien_trinh_loi
    movl $1, 92(%r12)
    leaq 320(%r12), %rdi
    movl $2, %esi                 // POSIX_SPAWN_SETPGROUP
    call VM_PROC_LIB(posix_spawnattr_setflags)
    testl %eax, %eax
    jnz Lpx_tao_tien_trinh_loi
    leaq 320(%r12), %rdi
    xorl %esi, %esi
    call VM_PROC_LIB(posix_spawnattr_setpgroup)
    testl %eax, %eax
    jnz Lpx_tao_tien_trinh_loi

#ifdef __APPLE__
    call VM_PROC_LIB(_NSGetEnviron)
    movq (%rax), %r9
#else
    movq environ@GOTPCREL(%rip), %rax
    movq (%rax), %r9
#endif
    movq %r9, 104(%r12)
    movl 44(%r12), %eax
    testl %eax, %eax
    je Lpx_moi_truong_san_sang
    cmpl $128, %eax
    ja Lpx_sai_moi_truong
    movl 24(%r12), %ecx
    testl $3, %ecx
    jnz Lpx_sai_moi_truong
    cmpl $(67000000-512), %ecx
    ja Lpx_sai_moi_truong
    leaq (%rbx,%rcx), %r10
    movq %r10, 120(%r12)
    movl 56(%r12), %edx
    testl $7, %edx
    jnz Lpx_sai_moi_truong
    cmpl $(67000000-33824), %edx
    ja Lpx_sai_moi_truong
    leaq (%rbx,%rdx), %r10
    movq %r10, 112(%r12)
    movl $0, 128(%r12)
    movl $0, 132(%r12)
Lpx_moi_truong_ke_thua:
    cmpl $4096, 128(%r12)
    jae Lpx_sai_moi_truong
    movq 104(%r12), %r10
    movl 128(%r12), %eax
    movq (%r10,%rax,8), %rdx
    testq %rdx, %rdx
    je Lpx_moi_truong_them_cuoi
    movl $0, 136(%r12)
Lpx_moi_truong_kiem_tra:
    movl 136(%r12), %eax
    cmpl 44(%r12), %eax
    jae Lpx_moi_truong_giu
    movq 120(%r12), %r10
    movl (%r10,%rax,4), %ecx
    cmpl $8388624, %ecx
    jb Lpx_sai_moi_truong
    cmpl $(67000000-4096), %ecx
    ja Lpx_sai_moi_truong
    leaq (%rbx,%rcx), %r8
    xorl %r9d, %r9d
Lpx_moi_truong_so_sanh:
    movzbl (%r8,%r9), %eax
    cmpl $61, %eax
    je Lpx_moi_truong_khoa_cuoi
    cmpb (%rdx,%r9), %al
    jne Lpx_moi_truong_tiep_ghi_de
    incl %r9d
    cmpl $4096, %r9d
    jb Lpx_moi_truong_so_sanh
    jmp Lpx_sai_moi_truong
Lpx_moi_truong_khoa_cuoi:
    cmpb $61, (%rdx,%r9)
    je Lpx_moi_truong_bo_qua
Lpx_moi_truong_tiep_ghi_de:
    incl 136(%r12)
    jmp Lpx_moi_truong_kiem_tra
Lpx_moi_truong_giu:
    movq 112(%r12), %r10
    movl 132(%r12), %eax
    movq %rdx, (%r10,%rax,8)
    incl 132(%r12)
Lpx_moi_truong_bo_qua:
    incl 128(%r12)
    jmp Lpx_moi_truong_ke_thua
Lpx_moi_truong_them_cuoi:
    xorl %r14d, %r14d
Lpx_moi_truong_them:
    cmpl 44(%r12), %r14d
    jge Lpx_moi_truong_cham_dut
    movq 120(%r12), %r10
    movl (%r10,%r14,4), %ecx
    cmpl $8388624, %ecx
    jb Lpx_sai_moi_truong
    cmpl $(67000000-4096), %ecx
    ja Lpx_sai_moi_truong
    leaq (%rbx,%rcx), %rdx
    movq 112(%r12), %r10
    movl 132(%r12), %eax
    movq %rdx, (%r10,%rax,8)
    incl 132(%r12)
    incl %r14d
    jmp Lpx_moi_truong_them
Lpx_moi_truong_cham_dut:
    movq 112(%r12), %r10
    movl 132(%r12), %eax
    movq $0, (%r10,%rax,8)
    movq %r10, 104(%r12)
Lpx_moi_truong_san_sang:
    movl 16(%r12), %eax
    leaq (%rbx,%rax), %r8
    movq (%r8), %rsi
    movq %r12, %rdi
    leaq 192(%r12), %rdx
    leaq 320(%r12), %rcx
    movq 104(%r12), %r9
    call VM_PROC_LIB(posix_spawnp)
    testl %eax, %eax
    jnz Lpx_tao_tien_trinh_loi
    movl $1, 96(%r12)
    jmp Lpx_tao_tien_trinh_hoan_tat
Lpx_sai_moi_truong:
    movl $22, %eax
    jmp Lpx_tao_tien_trinh_loi
Lpx_tao_tien_trinh_errno:
    PX_MA_LOI
Lpx_tao_tien_trinh_loi:
    movl %eax, 48(%r12)
    movl $0, 96(%r12)
Lpx_tao_tien_trinh_hoan_tat:
    cmpl $0, 92(%r12)
    je 1f
    leaq 320(%r12), %rdi
    call VM_PROC_LIB(posix_spawnattr_destroy)
1:
    cmpl $0, 88(%r12)
    je 2f
    leaq 192(%r12), %rdi
    call VM_PROC_LIB(posix_spawn_file_actions_destroy)
2:
    movl 68(%r12), %edi
    cmpl $0, %edi
    jl 3f
    call VM_PROC_LIB(close)
3:
    movl 76(%r12), %edi
    cmpl $0, %edi
    jl 4f
    call VM_PROC_LIB(close)
4:
    movl $-1, 68(%r12)
    movl $-1, 76(%r12)
    cmpl $0, 96(%r12)
    je Lpx_tao_tien_trinh_da_loi
    movl 64(%r12), %eax
    movl %eax, 4(%r12)
    movl 72(%r12), %eax
    movl %eax, 8(%r12)
    movl $1, %eax
    PX_TRA_VE
Lpx_tao_tien_trinh_da_loi:
    movl 64(%r12), %edi
    cmpl $0, %edi
    jl 5f
    call VM_PROC_LIB(close)
5:
    movl 72(%r12), %edi
    cmpl $0, %edi
    jl 6f
    call VM_PROC_LIB(close)
6:
    xorl %eax, %eax
    PX_TRA_VE

Lpx_tham_do:
    xorl %r15d, %r15d
    movl 4(%r12), %eax
    testl %eax, %eax
    js 1f
    movl %eax, 64(%r12)
    movw $1, 68(%r12)
    movw $0, 70(%r12)
    incl %r15d
1:
    movl 8(%r12), %eax
    testl %eax, %eax
    js 2f
    movl %eax, 64(%r12,%r15,8)
    movw $1, 68(%r12,%r15,8)
    movw $0, 70(%r12,%r15,8)
    incl %r15d
2:
    testl %r15d, %r15d
    je Lpx_tham_do_het_tep
    movl 28(%r12), %edx
    cmpl $5000, %edx
    ja Lpx_khong_hop_le
    movq %r12, %rdi
    addq $64, %rdi
    movl %r15d, %esi
    call VM_PROC_LIB(poll)
    testl %eax, %eax
    js Lpx_nhap_xuat_errno
    xorl %eax, %eax
    movl 4(%r12), %ecx
    testl %ecx, %ecx
    js 3f
    testw $0x39, 70(%r12)
    setnz %al
3:
    movl 8(%r12), %ecx
    testl %ecx, %ecx
    js 4f
    movl 4(%r12), %ecx
    testl %ecx, %ecx
    jns 5f
    testw $0x39, 70(%r12)
    jmp 6f
5:
    testw $0x39, 78(%r12)
6:
    setnz %cl
    movzbl %cl, %ecx
    leal (%rax,%rcx,2), %eax
4:
    PX_TRA_VE
Lpx_tham_do_het_tep:
    movl $4, %eax
    PX_TRA_VE

Lpx_doc:
    movl 40(%r12), %r14d
    cmpl $1, %r14d
    je 1f
    cmpl $2, %r14d
    jne Lpx_khong_hop_le
1:
    movl 36(%r12), %r15d
    cmpl $1, %r15d
    jl Lpx_khong_hop_le
    cmpl $4096, %r15d
    jg Lpx_khong_hop_le
    movl 32(%r12), %eax
    cmpl $(67000000-4096), %eax
    ja Lpx_khong_hop_le
    leal (%rax,%r15), %ecx
    cmpl $67000000, %ecx
    ja Lpx_khong_hop_le
    leaq (%rbx,%rax), %r15
    leaq (%r12,%r14,4), %r14
    movl (%r14), %edi
    testl %edi, %edi
    js Lpx_doc_het_tep
Lpx_doc_thu_lai:
    movq %r15, %rsi
    movl 36(%r12), %edx
    call VM_PROC_LIB(read)
    testq %rax, %rax
    jg Lpx_tra_ve
    je Lpx_doc_da_dong
    PX_MA_LOI
    cmpl $4, %eax
    je Lpx_doc_lap_lai
    negl %eax
    PX_TRA_VE
Lpx_doc_lap_lai:
    movl (%r14), %edi
    jmp Lpx_doc_thu_lai
Lpx_doc_da_dong:
    movl (%r14), %edi
    call VM_PROC_LIB(close)
    movl $-1, (%r14)
Lpx_doc_het_tep:
    xorl %eax, %eax
    PX_TRA_VE

Lpx_doi:
    movl (%r12), %r14d
    testl %r14d, %r14d
    jle Lpx_khong_hop_le
    cmpl $0, 4(%r12)
    jge Lpx_khong_hop_le
    cmpl $0, 8(%r12)
    jge Lpx_khong_hop_le
Lpx_doi_thu_lai:
    movl %r14d, %edi
    leaq 100(%r12), %rsi
    xorl %edx, %edx
    call VM_PROC_LIB(waitpid)
    testl %eax, %eax
    jg Lpx_doi_xong
    PX_MA_LOI
    cmpl $4, %eax
    je Lpx_doi_thu_lai
    negl %eax
    PX_TRA_VE
Lpx_doi_xong:
    movl $0, (%r12)
    movl 100(%r12), %edx
    movl %edx, %eax
    andl $127, %eax
    testl %eax, %eax
    je Lpx_doi_binh_thuong
    cmpl $127, %eax
    je Lpx_khong_hop_le
    addl $128, %eax
    PX_TRA_VE
Lpx_doi_binh_thuong:
    movl %edx, %eax
    shrl $8, %eax
    andl $255, %eax
    PX_TRA_VE

Lpx_dong:
    movl 4(%r12), %edi
    testl %edi, %edi
    js 1f
    call VM_PROC_LIB(close)
1:
    movl 8(%r12), %edi
    testl %edi, %edi
    js 2f
    call VM_PROC_LIB(close)
2:
    movl $-1, 4(%r12)
    movl $-1, 8(%r12)
    movl (%r12), %r14d
    movl $0, (%r12)
    testl %r14d, %r14d
    jle Lpx_dong_xong
    movl %r14d, %edi
    negl %edi
    movl $9, %esi
    call VM_PROC_LIB(kill)
Lpx_dong_doi:
    movl %r14d, %edi
    leaq 100(%r12), %rsi
    xorl %edx, %edx
    call VM_PROC_LIB(waitpid)
    testl %eax, %eax
    jns Lpx_dong_xong
    PX_MA_LOI
    cmpl $4, %eax
    je Lpx_dong_doi
Lpx_dong_xong:
    xorl %eax, %eax
    PX_TRA_VE

Lpx_nhap_xuat_errno:
    PX_MA_LOI
// poll bị ngắt trả về chưa sẵn sàng; V++ quản lý thời hạn.
    cmpl $4, %eax
    je Lpx_tham_do_bi_ngat
    negl %eax
    PX_TRA_VE
Lpx_tham_do_bi_ngat:
    xorl %eax, %eax
    PX_TRA_VE
Lpx_khong_hop_le:
    movl $-22, %eax
Lpx_tra_ve:
    PX_TRA_VE
