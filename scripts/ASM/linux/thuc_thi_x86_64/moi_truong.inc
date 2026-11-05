// Tìm và chép giá trị môi trường trong giới hạn vùng nhớ.

// Chép giá trị môi trường kết thúc NUL vào ô 4096 byte. Thiếu biến hoặc giá trị quá dài trả -1.
.Lchep_moi_truong_gia_tri:
    xorl %eax, %eax
1:  movb (%rsi,%rax), %cl
    movb %cl, (%rdi,%rax)
    testb %cl, %cl
    je 2f
    incl %eax
    cmpl $4095, %eax
    jbe 1b
    movb $0, (%rdi)
    movl $-1, %eax
2:  ret
// rdi giữ tên đã kiểm tra và kết thúc NUL; kết quả là con trỏ giá trị theo quy ước getenv.
.Ltim_moi_truong_gia_tri:
    movq vpp_linux_envp(%rip), %r8
    testq %r8, %r8
    je .Lmoi_truong_thieu
.Lmoi_truong_tiep:
    movq (%r8), %r10
    testq %r10, %r10
    je .Lmoi_truong_thieu
    xorl %ecx, %ecx
.Lmoi_truong_so_sanh:
    movb (%rdi,%rcx), %al
    testb %al, %al
    je .Lmoi_truong_khoa_cuoi
    cmpb %al, (%r10,%rcx)
    jne .Lmoi_truong_bo_qua
    incq %rcx
    jmp .Lmoi_truong_so_sanh
.Lmoi_truong_khoa_cuoi:
    cmpb $61, (%r10,%rcx)
    je .Lmoi_truong_tim_thay
.Lmoi_truong_bo_qua:
    addq $8, %r8
    jmp .Lmoi_truong_tiep
.Lmoi_truong_tim_thay:
    leaq 1(%r10,%rcx), %rax
    ret
.Lmoi_truong_thieu:
    xorl %eax, %eax
    ret

.globl vpp_native_os_env_copy
vpp_native_os_env_copy:
    VPP_VAO_BO_NHO
    VPP_KIEM_TRA_DUONG %edi
    VPP_KIEM_TRA_LECH %esi, VPP_LINUX_PATH_MAX
    leaq (%r11,%rsi), %rax
    movq %rax, -32(%rbp)
    leaq (%r11,%rdi), %rdi
    call .Ltim_moi_truong_gia_tri
    testq %rax, %rax
    je .Lbo_nho_khong_hop_le
    movq %rax, %rsi
    movq -32(%rbp), %rdi
    call .Lchep_moi_truong_gia_tri
    VPP_TRA_BO_NHO
.globl vpp_native_os_home
vpp_native_os_home:
    VPP_VAO_BO_NHO
    VPP_KIEM_TRA_LECH %edi, VPP_LINUX_PATH_MAX
    leaq (%r11,%rdi), %rax
    movq %rax, -32(%rbp)
    leaq .Lthu_muc_nha_ten(%rip), %rdi
    call .Ltim_moi_truong_gia_tri
    testq %rax, %rax
    je .Lbo_nho_khong_hop_le
    movq %rax, %rsi
    movq -32(%rbp), %rdi
    call .Lchep_moi_truong_gia_tri
    VPP_TRA_BO_NHO
.globl vpp_native_os_cwd
vpp_native_os_cwd:
    VPP_VAO_BO_NHO
    VPP_KIEM_TRA_LECH %edi, VPP_LINUX_PATH_MAX
    leaq (%r11,%rdi), %rdi
    movl $VPP_LINUX_PATH_MAX, %esi
    call vpp_sys_getcwd
    cmpq $-4095, %rax
    jae .Lthu_muc_hien_tai_loi
    decq %rax                 // getcwd tính cả NUL kết thúc
    VPP_TRA_BO_NHO
.Lthu_muc_hien_tai_loi:
    VPP_TRA_SYSCALL
.globl vpp_native_os_executable_path
vpp_native_os_executable_path:
    VPP_VAO_BO_NHO
    VPP_KIEM_TRA_LECH %edi, VPP_LINUX_PATH_MAX
    leaq (%r11,%rdi), %rdx
    movq %rdx, -32(%rbp)
    movq $-100, %rdi
    leaq .Lban_than_chuong_trinh(%rip), %rsi
    movl $4095, %ecx
    call vpp_sys_readlinkat
    cmpq $-4095, %rax
    jae .Lchuong_trinh_loi
    cmpq $4095, %rax
    jae .Lchuong_trinh_qua_dai
    movq -32(%rbp), %rdi
    movb $0, (%rdi,%rax)
    VPP_TRA_BO_NHO
.Lchuong_trinh_qua_dai:
    movl $36, vpp_linux_errno(%rip)  // ENAMETOOLONG
    movl $-1, %eax
    VPP_TRA_BO_NHO
.Lchuong_trinh_loi:
    VPP_TRA_SYSCALL

