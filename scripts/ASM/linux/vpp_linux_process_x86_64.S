// ABI tiến trình Linux x86-64 độc lập cho tiến trình máy.vi. Khối điều khiển 1024 byte, bảng con trỏ
// và độ lệch argv/env đều thuộc vùng nhớ V++. Lệnh: 0 tạo, 1 thăm dò, 2 đọc, 3 đợi, 4 đóng. Lỗi trả
// -errno; riêng tạo trả 0 và lưu errno dương tại +48.
#if !defined(__linux__) || !defined(__x86_64__)
#error Chỉ hỗ trợ Linux x86-64
#endif
#ifndef PR_ARENA
#define PR_ARENA 135271424
#endif
#define PR_PATH_MAX 4096
.text
.p2align 4
.globl vpp_native_os_process
.type vpp_native_os_process, @function
vpp_native_os_process:
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
    call vpp_native_memory_base
    movq %rax, %rbx
    testq %rax, %rax
    je .Ltien_trinh_khong_hop_le
    cmpl $4, %r13d
    ja .Ltien_trinh_khong_hop_le
    cmpl $(PR_ARENA-1024), %r14d
    ja .Ltien_trinh_khong_hop_le
    testl $15, %r14d
    jnz .Ltien_trinh_khong_hop_le
    leaq (%rbx,%r14), %r12
    testl %r13d, %r13d
    je .Ltien_trinh_tao
    cmpl $1, %r13d
    je .Ltien_trinh_tham_do
    cmpl $2, %r13d
    je .Ltien_trinh_doc
    cmpl $3, %r13d
    je .Ltien_trinh_doi
    jmp .Ltien_trinh_dong

.Ltien_trinh_tao:
    movl $0, 48(%r12)
    movl 20(%r12), %r15d
    cmpl $1, %r15d
    jl .Ltien_trinh_tao_khong_hop_le
    cmpl $128, %r15d
    jg .Ltien_trinh_tao_khong_hop_le
    movl 12(%r12), %eax
    testl $3, %eax
    jne .Ltien_trinh_tao_khong_hop_le
    cmpl $(PR_ARENA-512), %eax
    ja .Ltien_trinh_tao_khong_hop_le
    movl 16(%r12), %eax
    testl $7, %eax
    jne .Ltien_trinh_tao_khong_hop_le
    cmpl $(PR_ARENA-1032), %eax
    ja .Ltien_trinh_tao_khong_hop_le
    leaq (%rbx,%rax), %r8
    movq %r8, 144(%r12)       // Bảng con trỏ argv[] đã kiểm tra
    xorl %r14d, %r14d
.Ltien_trinh_doi_so_lap:
    cmpl %r15d, %r14d
    jae .Ltien_trinh_doi_so_xong
    movl 12(%r12), %eax
    leaq (%rbx,%rax), %r10
    movl (%r10,%r14,4), %eax
    cmpl $8388624, %eax
    jb .Ltien_trinh_tao_khong_hop_le
    cmpl $(PR_ARENA-PR_PATH_MAX), %eax
    ja .Ltien_trinh_tao_khong_hop_le
    leaq (%rbx,%rax), %rdx
    xorl %ecx, %ecx
.Ltien_trinh_kiem_tra_doi_so_nul:
    cmpb $0, (%rdx,%rcx)
    je .Ltien_trinh_doi_so_san_sang
    incq %rcx
    cmpq $PR_PATH_MAX, %rcx
    jb .Ltien_trinh_kiem_tra_doi_so_nul
    jmp .Ltien_trinh_tao_khong_hop_le
.Ltien_trinh_doi_so_san_sang:
// Từ chối tên chương trình rỗng trước fork/execve.
    testl %r14d, %r14d
    jne .Ltien_trinh_doi_so_luu
    cmpb $0, (%rdx)
    je .Ltien_trinh_tao_khong_hop_le
.Ltien_trinh_doi_so_luu:
    movq 144(%r12), %r8
    movq %rdx, (%r8,%r14,8)
    incl %r14d
    jmp .Ltien_trinh_doi_so_lap
.Ltien_trinh_doi_so_xong:
    movq 144(%r12), %r8
    movq $0, (%r8,%r14,8)
// Bộ biên dịch dùng envp ban đầu của nhân; chương trình độc lập lấy từ _start. Bộ kiểm tra dùng libc
// đọc environ.
#ifdef VPP_PROCESS_HOST_INITIAL_ENVP
    call Llinux_ban_dau_envp
#ifdef VPP_PROCESS_HOST_ENVIRON
#error Chỉ chọn một nguồn môi trường tiến trình Linux
#endif
#elif defined(VPP_PROCESS_HOST_ENVIRON)
    movq environ@GOTPCREL(%rip), %rax
    movq (%rax), %rax
#else
    movq vpp_linux_envp(%rip), %rax
#endif
    movq %rax, 152(%r12)
    movl 44(%r12), %r15d
    testl %r15d, %r15d
    je .Ltien_trinh_moi_truong_xong
    cmpl $128, %r15d
    ja .Ltien_trinh_tao_khong_hop_le
    movl 24(%r12), %eax
    testl $3, %eax
    jne .Ltien_trinh_tao_khong_hop_le
    cmpl $(PR_ARENA-512), %eax
    ja .Ltien_trinh_tao_khong_hop_le
    movl 56(%r12), %eax
    testl $7, %eax
    jne .Ltien_trinh_tao_khong_hop_le
    cmpl $(PR_ARENA-33800), %eax
    ja .Ltien_trinh_tao_khong_hop_le
    leaq (%rbx,%rax), %r8
    movq %r8, 152(%r12)
    xorl %r14d, %r14d
.Ltien_trinh_moi_truong_xac_thuc:
    cmpl %r15d, %r14d
    jae .Ltien_trinh_moi_truong_ke_thua_khoi_tao
    movl 24(%r12), %eax
    leaq (%rbx,%rax), %r10
    movl (%r10,%r14,4), %eax
    cmpl $8388624, %eax
    jb .Ltien_trinh_tao_khong_hop_le
    cmpl $(PR_ARENA-PR_PATH_MAX), %eax
    ja .Ltien_trinh_tao_khong_hop_le
    leaq (%rbx,%rax), %r8
    xorl %ecx, %ecx
.Ltien_trinh_moi_truong_xac_thuc_byte:
    cmpq $PR_PATH_MAX, %rcx
    jae .Ltien_trinh_tao_khong_hop_le
    movzbl (%r8,%rcx), %eax
    cmpb $'=', %al
    je .Ltien_trinh_moi_truong_xac_thuc_bang
    testb %al, %al
    je .Ltien_trinh_tao_khong_hop_le
    incq %rcx
    jmp .Ltien_trinh_moi_truong_xac_thuc_byte
.Ltien_trinh_moi_truong_xac_thuc_bang:
    testq %rcx, %rcx
    je .Ltien_trinh_tao_khong_hop_le
.Ltien_trinh_moi_truong_xac_thuc_nul:
    cmpq $PR_PATH_MAX, %rcx
    jae .Ltien_trinh_tao_khong_hop_le
    cmpb $0, (%r8,%rcx)
    je .Ltien_trinh_moi_truong_xac_thuc_tiep
    incq %rcx
    jmp .Ltien_trinh_moi_truong_xac_thuc_nul
.Ltien_trinh_moi_truong_xac_thuc_tiep:
    incl %r14d
    jmp .Ltien_trinh_moi_truong_xac_thuc
.Ltien_trinh_moi_truong_ke_thua_khoi_tao:
    movl $0, 128(%r12)         // Chỉ số mục kế thừa
    movl $0, 132(%r12)         // Chỉ số đầu ra
.Ltien_trinh_moi_truong_ke_thua:
    cmpl $4096, 128(%r12)
    ja .Ltien_trinh_tao_khong_hop_le
#ifdef VPP_PROCESS_HOST_INITIAL_ENVP
    call Llinux_ban_dau_envp
    movq %rax, %r10
#elif defined(VPP_PROCESS_HOST_ENVIRON)
    movq environ@GOTPCREL(%rip), %r10
    movq (%r10), %r10
#else
    movq vpp_linux_envp(%rip), %r10
#endif
    movl 128(%r12), %eax
    movq (%r10,%rax,8), %rdx
    testq %rdx, %rdx
    je .Ltien_trinh_moi_truong_them_cuoi_khoi_tao
// Đệm envp chứa tối đa 4096 mục kế thừa; từ chối mục thứ 4097.
    cmpl $4096, 128(%r12)
    jae .Ltien_trinh_tao_khong_hop_le
    movl $0, 136(%r12)         // Chỉ số mục ghi đè
.Ltien_trinh_moi_truong_so_sanh_tiep:
    movl 136(%r12), %eax
    cmpl %r15d, %eax
    jae .Ltien_trinh_moi_truong_giu
    movl 24(%r12), %ecx
    leaq (%rbx,%rcx), %r10
    movl (%r10,%rax,4), %ecx
    leaq (%rbx,%rcx), %r10
    xorl %ecx, %ecx
.Ltien_trinh_moi_truong_so_sanh_khoa:
    movzbl (%r10,%rcx), %eax
    cmpb $'=', %al
    je .Ltien_trinh_moi_truong_so_sanh_cuoi
    cmpb %al, (%rdx,%rcx)
    jne .Ltien_trinh_moi_truong_so_sanh_khong_thay
    incq %rcx
    jmp .Ltien_trinh_moi_truong_so_sanh_khoa
.Ltien_trinh_moi_truong_so_sanh_cuoi:
    cmpb $'=', (%rdx,%rcx)
    je .Ltien_trinh_moi_truong_bo_qua
.Ltien_trinh_moi_truong_so_sanh_khong_thay:
    incl 136(%r12)
    jmp .Ltien_trinh_moi_truong_so_sanh_tiep
.Ltien_trinh_moi_truong_giu:
    movq 152(%r12), %r10
    movl 132(%r12), %eax
    movq %rdx, (%r10,%rax,8)
    incl 132(%r12)
.Ltien_trinh_moi_truong_bo_qua:
    incl 128(%r12)
    jmp .Ltien_trinh_moi_truong_ke_thua
.Ltien_trinh_moi_truong_them_cuoi_khoi_tao:
    xorl %r14d, %r14d
.Ltien_trinh_moi_truong_them_cuoi:
    cmpl %r15d, %r14d
    jae .Ltien_trinh_moi_truong_cham_dut
    movl 24(%r12), %eax
    leaq (%rbx,%rax), %r10
    movl (%r10,%r14,4), %eax
    leaq (%rbx,%rax), %rdx
    movq 152(%r12), %r10
    movl 132(%r12), %eax
    movq %rdx, (%r10,%rax,8)
    incl 132(%r12)
    incl %r14d
    jmp .Ltien_trinh_moi_truong_them_cuoi
.Ltien_trinh_moi_truong_cham_dut:
    movq 152(%r12), %r10
    movl 132(%r12), %eax
    movq $0, (%r10,%rax,8)
.Ltien_trinh_moi_truong_xong:
    movl $-1, 64(%r12)
    movl $-1, 68(%r12)
    movl $-1, 72(%r12)
    movl $-1, 76(%r12)
    leaq 64(%r12), %rdi
    movl $524288, %esi         // O_CLOEXEC
    call vpp_sys_pipe2
    testq %rax, %rax
    js .Ltien_trinh_tao_errno
    leaq 72(%r12), %rdi
    movl $524288, %esi
    call vpp_sys_pipe2
    testq %rax, %rax
    js .Ltien_trinh_tao_errno
// Dời mô tả ống khỏi 0/1/2 để giữ đúng đích dup2.
    xorl %r14d, %r14d
.Ltien_trinh_doi_mo_ta:
    cmpl $4, %r14d
    jae .Ltien_trinh_tach
    movl 64(%r12,%r14,4), %edi
    cmpl $3, %edi
    jge .Ltien_trinh_doi_mo_ta_tiep
    movl $1030, %esi           // F_DUPFD_CLOEXEC
    movl $3, %edx
    call vpp_sys_fcntl
    testq %rax, %rax
    js .Ltien_trinh_tao_errno
    movl %eax, %r15d
    movl 64(%r12,%r14,4), %edi
    call vpp_sys_close
    movl %r15d, 64(%r12,%r14,4)
.Ltien_trinh_doi_mo_ta_tiep:
    incl %r14d
    jmp .Ltien_trinh_doi_mo_ta
.Ltien_trinh_tach:
    call vpp_sys_fork
    testq %rax, %rax
    js .Ltien_trinh_tao_errno
    je .Ltien_trinh_con
    movl %eax, (%r12)
    movl %eax, %edi
    movl %eax, %esi
    call vpp_sys_setpgid       // Con có thể đã exec; thao tác này vẫn an toàn
    movl 68(%r12), %edi
    call vpp_sys_close
    movl 76(%r12), %edi
    call vpp_sys_close
    movl $-1, 68(%r12)
    movl $-1, 76(%r12)
    movl 64(%r12), %eax
    movl %eax, 4(%r12)
    movl 72(%r12), %eax
    movl %eax, 8(%r12)
    movl $1, %eax
    jmp .Ltien_trinh_tra_ve
.Ltien_trinh_con:
    xorl %edi, %edi
    xorl %esi, %esi
    call vpp_sys_setpgid
    movl 68(%r12), %edi
    movl $1, %esi
    call vpp_sys_dup2
    testq %rax, %rax
    js .Ltien_trinh_con_that_bai
    movl 76(%r12), %edi
    movl $2, %esi
    call vpp_sys_dup2
    testq %rax, %rax
    js .Ltien_trinh_con_that_bai
    movl 64(%r12), %edi
    call vpp_sys_close
    movl 68(%r12), %edi
    call vpp_sys_close
    movl 72(%r12), %edi
    call vpp_sys_close
    movl 76(%r12), %edi
    call vpp_sys_close
    movq 144(%r12), %r14
    movq 152(%r12), %r15
    movq (%r14), %r13          // Chương trình
    cmpb $0, (%r13)
    je .Ltien_trinh_con_that_bai
    xorl %ecx, %ecx
.Ltien_trinh_chuong_trinh_quet:
    movb (%r13,%rcx), %al
    testb %al, %al
    je .Ltien_trinh_chuong_trinh_san_sang
    cmpb $'/', %al
    je .Ltien_trinh_con_truc_tiep
    incq %rcx
    jmp .Ltien_trinh_chuong_trinh_quet
.Ltien_trinh_con_truc_tiep:
    movq %r13, %rdi
    movq %r14, %rsi
    movq %r15, %rdx
    call vpp_sys_execve
    jmp .Ltien_trinh_con_that_bai
.Ltien_trinh_chuong_trinh_san_sang:
// Tìm PATH trong môi trường con đã ghép; nếu chưa đặt, dùng đường mặc định tuyệt đối theo execvp.
    movq %r15, %r8
.Ltien_trinh_tim_duong:
    movq (%r8), %r9
    testq %r9, %r9
    je .Ltien_trinh_mac_dinh_duong
    cmpb $'P', (%r9)
    jne .Ltien_trinh_tim_tiep
    cmpl $0x3d485441, 1(%r9)    // "ATH=" theo thứ tự byte thấp trước
    jne .Ltien_trinh_tim_tiep
    leaq 5(%r9), %r12
    jmp .Ltien_trinh_duong_khoi_dong
.Ltien_trinh_tim_tiep:
    addq $8, %r8
    jmp .Ltien_trinh_tim_duong
.Ltien_trinh_mac_dinh_duong:
    leaq .Ltien_trinh_mac_dinh_duong_chuoi(%rip), %r12
.Ltien_trinh_duong_khoi_dong:
    subq $4096, %rsp
    movq %rsp, %rbx
.Ltien_trinh_duong_doan:
    xorq %r8, %r8
.Ltien_trinh_duong_chep:
    movb (%r12), %al
    testb %al, %al
    je .Ltien_trinh_duong_thu
    cmpb $':', %al
    je .Ltien_trinh_duong_thu
    cmpl $4094, %r8d
    jae .Ltien_trinh_duong_bo_qua
    movb %al, (%rbx,%r8)
    incq %r8
    incq %r12
    jmp .Ltien_trinh_duong_chep
.Ltien_trinh_duong_bo_qua:
    cmpb $0, (%r12)
    je .Ltien_trinh_duong_tiep
    cmpb $':', (%r12)
    je .Ltien_trinh_duong_tiep
    incq %r12
    jmp .Ltien_trinh_duong_bo_qua
.Ltien_trinh_duong_thu:
    testq %r8, %r8
    je .Ltien_trinh_duong_thu_muc_hien_tai
    movq %r13, %r9
    xorq %rcx, %rcx
.Ltien_trinh_chuong_trinh_do_dai:
    cmpb $0, (%r9,%rcx)
    je .Ltien_trinh_do_dai_san_sang
    incq %rcx
    jmp .Ltien_trinh_chuong_trinh_do_dai
.Ltien_trinh_do_dai_san_sang:
    leaq 1(%r8,%rcx), %rax
    cmpq $4095, %rax
    ja .Ltien_trinh_duong_tiep
    movb $'/', (%rbx,%r8)
    incq %r8
.Ltien_trinh_duong_them_cuoi:
    movb (%r9), %al
    movb %al, (%rbx,%r8)
    incq %r9
    incq %r8
    testb %al, %al
    jne .Ltien_trinh_duong_them_cuoi
    movq %rbx, %rdi
    jmp .Ltien_trinh_duong_thuc_thi
.Ltien_trinh_duong_thu_muc_hien_tai:
    movq %r13, %rdi
.Ltien_trinh_duong_thuc_thi:
    movq %r14, %rsi
    movq %r15, %rdx
    call vpp_sys_execve
    cmpq $-2, %rax            // ENOENT
    je .Ltien_trinh_duong_tiep
    cmpq $-13, %rax           // EACCES
    je .Ltien_trinh_duong_tiep
    cmpq $-20, %rax           // ENOTDIR
    jne .Ltien_trinh_con_that_bai
.Ltien_trinh_duong_tiep:
    cmpb $0, (%r12)
    je .Ltien_trinh_con_that_bai
    incq %r12
    jmp .Ltien_trinh_duong_doan
.Ltien_trinh_con_that_bai:
    movl $127, %edi
    call vpp_sys_exit_group
    ud2
.Ltien_trinh_tao_khong_hop_le:
// Sau khi tạo thất bại, V++ đọc control[12], tức byte +48.
    movl $22, 48(%r12)          // EINVAL
    movl $-1, 4(%r12)
    movl $-1, 8(%r12)
    xorl %eax, %eax
    jmp .Ltien_trinh_tra_ve
.Ltien_trinh_tao_errno:
    negl %eax
    movl %eax, 48(%r12)
    jmp .Ltien_trinh_tao_don_dep
.Ltien_trinh_tao_don_dep:
    xorl %r14d, %r14d
.Ltien_trinh_don_dep_lap:
    cmpl $4, %r14d
    jae .Ltien_trinh_tao_da_loi
    movl 64(%r12,%r14,4), %edi
    testl %edi, %edi
    js .Ltien_trinh_don_dep_tiep
    call vpp_sys_close
.Ltien_trinh_don_dep_tiep:
    incl %r14d
    jmp .Ltien_trinh_don_dep_lap
.Ltien_trinh_tao_da_loi:
    movl $-1, 4(%r12)
    movl $-1, 8(%r12)
    xorl %eax, %eax
    jmp .Ltien_trinh_tra_ve

.Ltien_trinh_tham_do:
    xorl %r15d, %r15d
    movl 4(%r12), %eax
    testl %eax, %eax
    js .Ltien_trinh_tham_do_stderr
    movl %eax, 64(%r12)
    movw $1, 68(%r12)          // POLLIN
    movw $0, 70(%r12)
    incl %r15d
.Ltien_trinh_tham_do_stderr:
    movl 8(%r12), %eax
    testl %eax, %eax
    js .Ltien_trinh_tham_do_goi
    movl %eax, 64(%r12,%r15,8)
    movw $1, 68(%r12,%r15,8)
    movw $0, 70(%r12,%r15,8)
    incl %r15d
.Ltien_trinh_tham_do_goi:
    testl %r15d, %r15d
    je .Ltien_trinh_tham_do_het_tep
    movl 28(%r12), %edx
    cmpl $5000, %edx
    ja .Ltien_trinh_khong_hop_le
    leaq 64(%r12), %rdi
    movl %r15d, %esi
    call vpp_sys_poll
    cmpq $-4, %rax           // EINTR được coi là chưa sẵn sàng
    je .Ltien_trinh_tham_do_bi_ngat
    testq %rax, %rax
    js .Ltien_trinh_tra_ve
    xorl %eax, %eax
    cmpl $0, 4(%r12)
    jl .Ltien_trinh_tham_do_kiem_tra_ma_loi
    testw $0x39, 70(%r12)     // POLLIN|POLLERR|POLLHUP|POLLNVAL
    setnz %al
.Ltien_trinh_tham_do_kiem_tra_ma_loi:
    cmpl $0, 8(%r12)
    jl .Ltien_trinh_tra_ve
    cmpl $0, 4(%r12)
    jl .Ltien_trinh_tham_do_chi_ma_loi
    testw $0x39, 78(%r12)
    jmp .Ltien_trinh_tham_do_ma_loi_ket_qua
.Ltien_trinh_tham_do_chi_ma_loi:
    testw $0x39, 70(%r12)
.Ltien_trinh_tham_do_ma_loi_ket_qua:
    setnz %cl
    movzbl %cl, %ecx
    leal (%rax,%rcx,2), %eax
    jmp .Ltien_trinh_tra_ve
.Ltien_trinh_tham_do_het_tep:
    movl $4, %eax
    jmp .Ltien_trinh_tra_ve
.Ltien_trinh_tham_do_bi_ngat:
    xorl %eax, %eax
    jmp .Ltien_trinh_tra_ve

.Ltien_trinh_doc:
    movl 40(%r12), %r14d
    cmpl $1, %r14d
    je .Ltien_trinh_doc_da_kiem_tra_kenh
    cmpl $2, %r14d
    jne .Ltien_trinh_khong_hop_le
.Ltien_trinh_doc_da_kiem_tra_kenh:
    movl 36(%r12), %edx
    cmpl $1, %edx
    jl .Ltien_trinh_khong_hop_le
    cmpl $4096, %edx
    jg .Ltien_trinh_khong_hop_le
    movl 32(%r12), %eax
    cmpl $(PR_ARENA-4096), %eax
    ja .Ltien_trinh_khong_hop_le
    leaq (%rbx,%rax), %r15
    leaq (%r12,%r14,4), %r14
    movl (%r14), %edi
    testl %edi, %edi
    js .Ltien_trinh_doc_het_tep
.Ltien_trinh_doc_thu_lai:
    movq %r15, %rsi
    movl 36(%r12), %edx
    call vpp_sys_read
    testq %rax, %rax
    jg .Ltien_trinh_tra_ve
    je .Ltien_trinh_doc_da_dong
    cmpq $-4, %rax
    je .Ltien_trinh_doc_thu_lai
    jmp .Ltien_trinh_tra_ve
.Ltien_trinh_doc_da_dong:
    movl (%r14), %edi
    call vpp_sys_close
    movl $-1, (%r14)
.Ltien_trinh_doc_het_tep:
    xorl %eax, %eax
    jmp .Ltien_trinh_tra_ve

.Ltien_trinh_doi:
    movl (%r12), %r14d
    testl %r14d, %r14d
    jle .Ltien_trinh_khong_hop_le
    cmpl $0, 4(%r12)
    jge .Ltien_trinh_khong_hop_le
    cmpl $0, 8(%r12)
    jge .Ltien_trinh_khong_hop_le
.Ltien_trinh_doi_thu_lai:
    movl %r14d, %edi
    leaq 100(%r12), %rsi
    xorl %edx, %edx
    xorl %ecx, %ecx
    call vpp_sys_wait4
    cmpq $-4, %rax
    je .Ltien_trinh_doi_thu_lai
    testq %rax, %rax
    js .Ltien_trinh_tra_ve
    movl $0, (%r12)
    movl 100(%r12), %edx
    movl %edx, %eax
    andl $127, %eax
    testl %eax, %eax
    je .Ltien_trinh_doi_binh_thuong
    cmpl $127, %eax
    je .Ltien_trinh_khong_hop_le
    addl $128, %eax
    jmp .Ltien_trinh_tra_ve
.Ltien_trinh_doi_binh_thuong:
    movl %edx, %eax
    shrl $8, %eax
    andl $255, %eax
    jmp .Ltien_trinh_tra_ve

.Ltien_trinh_dong:
    movl 4(%r12), %edi
    testl %edi, %edi
    js .Ltien_trinh_dong_ma_loi
    call vpp_sys_close
.Ltien_trinh_dong_ma_loi:
    movl 8(%r12), %edi
    testl %edi, %edi
    js .Ltien_trinh_dong_cac_mo_ta_xong
    call vpp_sys_close
.Ltien_trinh_dong_cac_mo_ta_xong:
    movl $-1, 4(%r12)
    movl $-1, 8(%r12)
    movl (%r12), %r14d
    movl $0, (%r12)
    testl %r14d, %r14d
    jle .Ltien_trinh_dong_xong
    movl %r14d, %edi
    call vpp_sys_getpgid
    cmpl %r14d, %eax
    jne .Ltien_trinh_dung_con
    movl %r14d, %edi
    negl %edi                 // Gửi tới nhóm tiến trình riêng đã kiểm tra
    movl $9, %esi             // SIGKILL cho nhóm tiến trình riêng
    call vpp_sys_kill
    jmp .Ltien_trinh_dong_doi
.Ltien_trinh_dung_con:
    movl %r14d, %edi           // Dự phòng nếu setpgid chưa thành công
    movl $9, %esi
    call vpp_sys_kill
.Ltien_trinh_dong_doi:
    movl %r14d, %edi
    leaq 100(%r12), %rsi
    xorl %edx, %edx
    xorl %ecx, %ecx
    call vpp_sys_wait4
    cmpq $-4, %rax
    je .Ltien_trinh_dong_doi
.Ltien_trinh_dong_xong:
    xorl %eax, %eax
    jmp .Ltien_trinh_tra_ve

.Ltien_trinh_khong_hop_le:
// Độ lệch khối điều khiển sai không cho phép ghi errno an toàn.
    movl $-22, %eax
.Ltien_trinh_tra_ve:
    addq $8, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.size vpp_native_os_process, .-vpp_native_os_process

.section .rodata
.Ltien_trinh_mac_dinh_duong_chuoi: .asciz "/bin:/usr/bin"
.section .note.GNU-stack,"",@progbits
