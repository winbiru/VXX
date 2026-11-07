// Tạo, thăm dò, đọc, đợi và đóng tiến trình theo ABI của nền tảng.

// V++ cấp bảng argv ở độ lệch 0x04001400; linker cũng dùng vùng riêng tại
// 0x08001400 để không chồng lấn với các bảng tiến trình hiện có.
.globl PUBLIC(vpp_native_os_spawn_clang)
PUBLIC(vpp_native_os_spawn_clang):
    VAO_BO_NHO
    cmpl $0x04001400, %edi
    je .Lclang_argv_offset_valid
    cmpl $0x08001400, %edi
    jne Lbo_nho_loi
.Lclang_argv_offset_valid:
    KIEM_TRA_LECH %edi, 32768   // Đúng 4096 con trỏ argv đủ độ rộng
    leaq (%r11,%rdi), %rax
    cmpq $0, (%rax)
    je Lbo_nho_loi
    movq %rax, -32(%rbp)
#ifdef __APPLE__
    call LIB(_NSGetEnviron)
    movq (%rax), %r9
    leaq -40(%rbp), %rdi       // pid_t *
    leaq Lclang_duong(%rip), %rsi
    xorl %edx, %edx           // Thao tác tệp
    xorl %ecx, %ecx           // Thuộc tính
    movq -32(%rbp), %r8       // argv
    call LIB(posix_spawn)
    testl %eax, %eax
    jne Ltao_tien_trinh_that_bai
    movl -40(%rbp), %eax
    TRA_BO_NHO
#else
// Kiểm tra mọi con trỏ và dấu kết thúc chuỗi trước fork; execve của tiến trình con chỉ nhận con trỏ
// nằm trong vùng nhớ V++.
    movq %rax, %r8
    leaq 32768(%r8), %r9
.Lclang_xac_thuc:
    cmpq %r9, %r8
    jae Lbo_nho_loi
    movq (%r8), %rax
    testq %rax, %rax
    je .Lclang_da_kiem_tra
    cmpq %r11, %rax
    jb Lbo_nho_loi
    leaq (VPP_ARENA_SIZE - 1)(%r11), %r10
    cmpq %r10, %rax
    ja Lbo_nho_loi
    leaq VPP_ARENA_SIZE(%r11), %rdx
    subq %rax, %rdx
    cmpq $VPP_PATH_MAX, %rdx
    jbe 1f
    movl $VPP_PATH_MAX, %edx
1:  xorl %ecx, %ecx
2:  cmpb $0, (%rax,%rcx)
    je 3f
    incq %rcx
    cmpq %rdx, %rcx
    jb 2b
    jmp Lbo_nho_loi
3:  addq $8, %r8
    jmp .Lclang_xac_thuc
.Lclang_da_kiem_tra:
    cmpq -32(%rbp), %r8
    je Lbo_nho_loi
    call Llinux_ban_dau_envp
    movq %rax, -40(%rbp)      // Môi trường kế thừa cho execve
// Giữ các thanh ghi mà bên được gọi phải bảo toàn theo SysV cho tiến trình cha.
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $8, %rsp             // Căn ngăn xếp trước lời gọi syscall
    call vpp_sys_fork
    testq %rax, %rax
    jne .Lclang_cha

// Tiến trình con tìm chương trình liên kết trong PATH bằng syscall trực tiếp.
    movq -32(%rbp), %rsi
    movq (%rsi), %r12         // argv[0]
    xorl %r15d, %r15d         // Độ dài tên chương trình
    xorl %ebx, %ebx          // Có dấu '/'
.Lclang_ten_quet:
    cmpq $4095, %r15
    jae .Lclang_con_that_bai
    movb (%r12,%r15), %al
    testb %al, %al
    je .Lclang_ten_xong
    cmpb $'/', %al
    jne 4f
    movl $1, %ebx
4:  incq %r15
    jmp .Lclang_ten_quet
.Lclang_ten_xong:
    testq %r15, %r15
    je .Lclang_con_that_bai
    testl %ebx, %ebx
    je .Lclang_tim_kiem_duong
    movq %r12, %rdi
    movq -32(%rbp), %rsi
    movq -40(%rbp), %rdx
    call vpp_sys_execve
    jmp .Lclang_con_that_bai

.Lclang_tim_kiem_duong:
    movq -40(%rbp), %r10
    xorl %ecx, %ecx
.Lclang_tim_duong:
    testq %r10, %r10
    je .Lclang_mac_dinh_duong
    cmpl $4096, %ecx
    jae .Lclang_mac_dinh_duong
    movq (%r10,%rcx,8), %rax
    testq %rax, %rax
    je .Lclang_mac_dinh_duong
    cmpl $0x48544150, (%rax) // 'PATH' theo thứ tự byte thấp trước
    jne .Lclang_duong_tiep
    cmpb $'=', 4(%rax)
    je .Lclang_duong_tim_thay
.Lclang_duong_tiep:
    incl %ecx
    jmp .Lclang_tim_duong
.Lclang_duong_tim_thay:
    leaq 5(%rax), %r13
    jmp .Lclang_duong_san_sang
.Lclang_mac_dinh_duong:
    leaq .Lclang_du_phong_duong(%rip), %r13
.Lclang_duong_san_sang:
    subq $4096, %rsp
    movq %rsp, %r14
.Lclang_duong_doan:
    xorl %r8d, %r8d
.Lclang_chep_thu_muc:
    movzbl (%r13), %eax
    testb %al, %al
    je .Lclang_lan_thu
    cmpb $':', %al
    je .Lclang_lan_thu
    cmpq $4094, %r8
    jae .Lclang_bo_qua_dai_thu_muc
    movb %al, (%r14,%r8)
    incq %r8
    incq %r13
    jmp .Lclang_chep_thu_muc
.Lclang_bo_qua_dai_thu_muc:
    cmpb $0, (%r13)
    je .Lclang_tiep_thu_muc
    cmpb $':', (%r13)
    je .Lclang_tiep_thu_muc
    incq %r13
    jmp .Lclang_bo_qua_dai_thu_muc
.Lclang_lan_thu:
    testq %r8, %r8
    jne .Lclang_them_cuoi_ten
    movq %r12, %rdi          // Mục PATH rỗng chỉ thư mục hiện tại
    jmp .Lclang_thu_thuc_thi
.Lclang_them_cuoi_ten:
    movl $4094, %eax
    subq %r15, %rax
    cmpq %rax, %r8
    ja .Lclang_tiep_thu_muc
    movb $'/', (%r14,%r8)
    incq %r8
    xorl %ecx, %ecx
.Lclang_chep_ten:
    movb (%r12,%rcx), %al
    movb %al, (%r14,%r8)
    incq %r8
    incq %rcx
    testb %al, %al
    jne .Lclang_chep_ten
    movq %r14, %rdi
.Lclang_thu_thuc_thi:
    movq -32(%rbp), %rsi
    movq -40(%rbp), %rdx
    call vpp_sys_execve
    cmpq $-2, %rax            // ENOENT
    je .Lclang_tiep_thu_muc
    cmpq $-13, %rax           // EACCES
    je .Lclang_tiep_thu_muc
    cmpq $-20, %rax           // ENOTDIR
    jne .Lclang_con_that_bai
.Lclang_tiep_thu_muc:
    cmpb $0, (%r13)
    je .Lclang_con_that_bai
    incq %r13
    jmp .Lclang_duong_doan
.Lclang_con_that_bai:
    movl $127, %edi
    call vpp_sys_exit_group
    ud2
.Lclang_cha:
    movq %rax, -48(%rbp)
    addq $8, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    movq -48(%rbp), %rax
    call Llinux_goi_he_thong_ket_qua
    TRA_BO_NHO
#endif
Ltao_tien_trinh_that_bai:
    movl $-1, %eax
    TRA_BO_NHO
#ifndef __APPLE__
.section .rodata
.Lclang_du_phong_duong: .asciz "/bin:/usr/bin"
.text
#endif
.globl PUBLIC(vpp_native_os_waitpid)
PUBLIC(vpp_native_os_waitpid):
    VAO_BO_NHO
    cmpl $1, %edi
    jl Lbo_nho_loi
    KIEM_TRA_LECH %esi, 4
    leaq (%r11,%rsi), %rsi
    xorl %edx, %edx
#ifdef __APPLE__
    call LIB(waitpid)
#else
    call Llinux_doi_pid
#endif
    TRA_BO_NHO
