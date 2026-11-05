// Tạo, thăm dò, đọc, đợi và đóng tiến trình theo ABI của nền tảng.

// fork/exec bảng argv do V++ chuẩn bị tại 0x04001400. Chỉ con tìm PATH; cha nhận PID và V++ đợi bằng
// wait4. exec thất bại thoát 127. Cầu khởi động và errno hiện dùng một luồng.
.globl vpp_native_os_spawn_clang
.type vpp_native_os_spawn_clang, @function
vpp_native_os_spawn_clang:
    VPP_VAO_BO_NHO
    cmpl $0x04001400, %edi
    jne .Lbo_nho_khong_hop_le
    VPP_KIEM_TRA_LECH %edi, 32768  // 4096 con trỏ argv đủ độ rộng
    leaq (%r11,%rdi), %r8
    movq %r8, -32(%rbp)
    leaq 32768(%r8), %r9      // Cuối bảng con trỏ argv được phép dùng
// Kiểm tra mọi con trỏ và dấu NUL trước fork để không đưa argv sai vào execve của tiến trình con.
.Ltao_tien_trinh_xac_thuc_doi_so:
    cmpq %r9, %r8
    jae .Lbo_nho_khong_hop_le        // argv phải có NULL kết thúc trong 4096 ô
    movq (%r8), %rax
    testq %rax, %rax
    je .Ltao_tien_trinh_cac_doi_so_da_kiem_tra
    cmpq %r11, %rax
    jb .Lbo_nho_khong_hop_le
    leaq (VPP_LINUX_ARENA_SIZE - 1)(%r11), %r10
    cmpq %r10, %rax
    ja .Lbo_nho_khong_hop_le
    leaq VPP_LINUX_ARENA_SIZE(%r11), %rdx
    subq %rax, %rdx
    cmpq $VPP_LINUX_PATH_MAX, %rdx
    jbe 1f
    movl $VPP_LINUX_PATH_MAX, %edx
1:  xorl %ecx, %ecx
2:  cmpb $0, (%rax,%rcx)
    je 3f
    incq %rcx
    cmpq %rdx, %rcx
    jb 2b
    jmp .Lbo_nho_khong_hop_le
3:  addq $8, %r8
    jmp .Ltao_tien_trinh_xac_thuc_doi_so
.Ltao_tien_trinh_cac_doi_so_da_kiem_tra:
    cmpq -32(%rbp), %r8     // Yêu cầu argv[0] khác NULL
    je .Lbo_nho_khong_hop_le
    call vpp_sys_fork
    testq %rax, %rax
    jne .Ltao_tien_trinh_cha

// Con không trở về V++; có thể dùng thanh ghi phải bảo toàn vì cha giữ bộ thanh ghi riêng.
    movq -32(%rbp), %rsi
    movq (%rsi), %r12            // argv[0]
    xorq %r15, %r15             // Độ dài tên lệnh theo byte
    xorl %ebx, %ebx             // Đã thấy dấu gạch chéo
.Ltao_tien_trinh_ten_quet:
    cmpq $4095, %r15
    jae .Ltao_tien_trinh_con_that_bai
    movb (%r12,%r15), %al
    testb %al, %al
    je .Ltao_tien_trinh_ten_san_sang
    cmpb $47, %al
    jne 1f
    movl $1, %ebx
1:  incq %r15
    jmp .Ltao_tien_trinh_ten_quet
.Ltao_tien_trinh_ten_san_sang:
    testq %r15, %r15
    je .Ltao_tien_trinh_con_that_bai
    testl %ebx, %ebx
    je .Ltao_tien_trinh_tim_duong
    movq %r12, %rdi
    movq -32(%rbp), %rsi
    movq vpp_linux_envp(%rip), %rdx
    call vpp_sys_execve
    jmp .Ltao_tien_trinh_con_that_bai     // Tên có '/' dùng đường trực tiếp

.Ltao_tien_trinh_tim_duong:
    cmpq $4094, %r15            // Chừa chỗ cho dấu gạch chéo và NUL
    ja .Ltao_tien_trinh_con_that_bai
    leaq .Lduong_ten(%rip), %rdi
    call .Ltim_moi_truong_gia_tri
    testq %rax, %rax
    jne 1f
    leaq .Lmac_dinh_duong(%rip), %rax
1:  movq %rax, %r13           // Vị trí đang duyệt PATH
    subq $4096, %rsp
    movq %rsp, %r14            // Đường ứng viên đã giới hạn độ dài
.Ltao_tien_trinh_duong_doan:
    xorq %r8, %r8              // Độ dài thư mục ứng viên
.Ltao_tien_trinh_duong_chep:
    movzbl (%r13), %eax
    testb %al, %al
    je .Ltao_tien_trinh_duong_thu
    cmpb $58, %al              // ':'
    je .Ltao_tien_trinh_duong_thu
    cmpq $4094, %r8
    jae .Ltao_tien_trinh_duong_bo_qua
    movb %al, (%r14,%r8)
    incq %r8
    incq %r13
    jmp .Ltao_tien_trinh_duong_chep
.Ltao_tien_trinh_duong_bo_qua:
    movzbl (%r13), %eax
    testb %al, %al
    je .Ltao_tien_trinh_duong_tiep
    cmpb $58, %al
    je .Ltao_tien_trinh_duong_tiep
    incq %r13
    jmp .Ltao_tien_trinh_duong_bo_qua
.Ltao_tien_trinh_duong_thu:
// Mục PATH rỗng yêu cầu tìm trong thư mục hiện tại.
    testq %r8, %r8
    jne .Ltao_tien_trinh_duong_tao
    movq %r12, %rdi
    jmp .Ltao_tien_trinh_duong_thuc_thi
.Ltao_tien_trinh_duong_tao:
    movl $4094, %eax           // Chừa chỗ cho dấu gạch chéo, tên và NUL
    subq %r15, %rax
    cmpq %rax, %r8
    ja .Ltao_tien_trinh_duong_tiep
    movb $47, (%r14,%r8)
    incq %r8
    xorq %rcx, %rcx
.Ltao_tien_trinh_duong_them_cuoi:
    movb (%r12,%rcx), %al
    movb %al, (%r14,%r8)
    incq %r8
    incq %rcx
    testb %al, %al
    jne .Ltao_tien_trinh_duong_them_cuoi
    movq %r14, %rdi
.Ltao_tien_trinh_duong_thuc_thi:
    movq -32(%rbp), %rsi
    movq vpp_linux_envp(%rip), %rdx
    call vpp_sys_execve
// Theo execvp, tiếp tục tìm nếu đường không tồn tại, không truy cập được hoặc không phải thư mục;
// lỗi khác kết thúc tiến trình con.
    cmpq $-2, %rax             // ENOENT
    je .Ltao_tien_trinh_duong_tiep
    cmpq $-13, %rax            // EACCES
    je .Ltao_tien_trinh_duong_tiep
    cmpq $-20, %rax            // ENOTDIR
    jne .Ltao_tien_trinh_con_that_bai
.Ltao_tien_trinh_duong_tiep:
    cmpb $0, (%r13)
    je .Ltao_tien_trinh_con_that_bai
    incq %r13
    jmp .Ltao_tien_trinh_duong_doan
.Ltao_tien_trinh_con_that_bai:
    movl $127, %edi
    call vpp_sys_exit_group
    ud2
.Ltao_tien_trinh_cha:
    VPP_TRA_SYSCALL
.size vpp_native_os_spawn_clang, .-vpp_native_os_spawn_clang

.globl vpp_native_os_waitpid
vpp_native_os_waitpid:
    VPP_VAO_BO_NHO
    cmpl $1, %edi
    jl .Lbo_nho_khong_hop_le
    VPP_KIEM_TRA_LECH %esi, 4
    leaq (%r11,%rsi), %rsi
    xorl %edx, %edx             // Tùy chọn = 0
.Ldoi_pid_thu_lai:
    xorl %ecx, %ecx             // syscall ghi đè rcx ở mỗi lần thử
    call vpp_sys_wait4
    cmpq $-4, %rax              // EINTR: vẫn sở hữu tiến trình con này
    je .Ldoi_pid_thu_lai
    VPP_TRA_SYSCALL

.section .rodata
.Lthu_muc_nha_ten: .asciz "HOME"
.Lduong_ten: .asciz "PATH"
.Lmac_dinh_duong: .asciz "/bin:/usr/bin"
.Lban_than_chuong_trinh: .asciz "/proc/self/exe"
.Ltao_bang_chu: .ascii "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
.text

