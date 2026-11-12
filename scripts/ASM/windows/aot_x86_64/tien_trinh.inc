// Tạo, thăm dò, đọc, đợi và đóng tiến trình theo ABI của nền tảng.

// V++ quản lý một tiến trình con với PID quy ước là 1. _wspawnvp tìm PATH và tạo dòng lệnh Unicode
// từ argv UTF-16; _cwait nhận mã thoát rồi đổi sang trạng thái đợi POSIX. Bảng argv gồm 4096 con trỏ
// tại độ lệch 0x04001400 hoặc 0x08001400.
.globl vpp_native_os_spawn_clang
vpp_native_os_spawn_clang:
    VAO_BO_NHO
    subq $64, %rsp           // Biến cục bộ bổ sung, giữ căn chỉnh Win64
    cmpl $0x04001400, %ecx
    je .Lclang_argv_offset_valid
    cmpl $0x08001400, %ecx
    jne Lbo_nho_loi
.Lclang_argv_offset_valid:
    cmpq $0, vpp_native_spawn_handle(%rip)
    jne Lbo_nho_loi
    leaq (%r11,%rcx), %r10
    movq %r10, -32(%rbp)
    leaq 32768(%r10), %r9
Ltao_tien_trinh_xac_thuc:
    cmpq %r9, %r10
    jae Lbo_nho_loi
    movq (%r10), %rax
    testq %rax, %rax
    je Ltao_tien_trinh_da_kiem_tra
    cmpq %r11, %rax
    jb Lbo_nho_loi
    leaq (VPP_ARENA_SIZE - 1)(%r11), %r8
    cmpq %r8, %rax
    ja Lbo_nho_loi
    leaq VPP_ARENA_SIZE(%r11), %rdx
    subq %rax, %rdx
    cmpq $4096, %rdx
    jbe 1f
    movl $4096, %edx
1:  xorl %ecx, %ecx
2:  cmpb $0, (%rax,%rcx)
    je 3f
    incq %rcx
    cmpq %rdx, %rcx
    jb 2b
    jmp Lbo_nho_loi
3:  addq $8, %r10
    jmp Ltao_tien_trinh_xac_thuc
Ltao_tien_trinh_da_kiem_tra:
    cmpq -32(%rbp), %r10       // argv[0] bắt buộc phải có
    je Lbo_nho_loi
    movq -32(%rbp), %r10
    movq (%r10), %r8
    cmpb $0, (%r8)
    je Lbo_nho_loi
    movq $0, -48(%rbp)        // Đường chương trình riêng, chưa trích dẫn
    xorl %ecx, %ecx
    movl $4096, %ecx
    movl $8, %edx
    call calloc                // Bảng wchar_t *argv[4096] kết thúc bằng 0
    testq %rax, %rax
    je Lbo_nho_loi
    movq %rax, -40(%rbp)
    movq $0, -64(%rbp)        // Chỉ số đối số
Ltao_tien_trinh_utf16_tiep:
    movq -32(%rbp), %r10
    movq -64(%rbp), %rcx
    movq (%r10,%rcx,8), %r8
    testq %r8, %r8
    je Ltao_tien_trinh_utf16_san_sang
    movq %r8, -72(%rbp)
    movl $8192, %ecx          // 4096 đơn vị mã UTF-16, kể cả NUL
    call malloc
    testq %rax, %rax
    je Ltao_tien_trinh_utf16_loi_thao_tac
    movq -40(%rbp), %r10
    movq -64(%rbp), %rdx
    movq %rax, (%r10,%rdx,8)
    movq -72(%rbp), %rcx
    movq %rax, %rdx
    call Lduong_utf8_sang_utf16
    testl %eax, %eax
    je Ltao_tien_trinh_utf16_loi_thao_tac
    movl $16392, %ecx        // Tối đa: gạch chéo ngược gấp đôi, dấu nháy và NUL
    call malloc
    testq %rax, %rax
    je Ltao_tien_trinh_utf16_loi_thao_tac
    movq %rax, -56(%rbp)
    movq -40(%rbp), %r10
    movq -64(%rbp), %rax
    movq (%r10,%rax,8), %rcx
    movq -56(%rbp), %rdx
    call Ltao_tien_trinh_trich_utf16
    cmpq $0, -64(%rbp)
    jne Ltao_tien_trinh_utf16_giai_phong_tho
    movq -40(%rbp), %r10
    movq (%r10), %rax
    movq %rax, -48(%rbp)      // Giữ đường chương trình chưa trích dẫn
    jmp Ltao_tien_trinh_utf16_dat_da_trich
Ltao_tien_trinh_utf16_giai_phong_tho:
    movq -40(%rbp), %r10
    movq -64(%rbp), %rdx
    movq (%r10,%rdx,8), %rcx
    call free
Ltao_tien_trinh_utf16_dat_da_trich:
    movq -40(%rbp), %r10
    movq -64(%rbp), %rdx
    movq -56(%rbp), %rax
    movq %rax, (%r10,%rdx,8)  // Bảng hiện sở hữu đối số đã trích dẫn
Ltao_tien_trinh_utf16_tien:
    incq -64(%rbp)
    jmp Ltao_tien_trinh_utf16_tiep
Ltao_tien_trinh_utf16_san_sang:
    movl $1, %ecx             // _P_NOWAIT
    movq -48(%rbp), %rdx      // Đường chương trình chưa trích dẫn
    movq -40(%rbp), %r8       // argv[0] đã trích dẫn, phần còn lại theo bảng argv
    call _wspawnvp
    movq %rax, -80(%rbp)
    jmp Ltao_tien_trinh_utf16_don_dep
Ltao_tien_trinh_utf16_loi_thao_tac:
    movq $-1, -80(%rbp)
Ltao_tien_trinh_utf16_don_dep:
    movq $0, -64(%rbp)
Ltao_tien_trinh_utf16_giai_phong_tiep:
    movq -40(%rbp), %r10
    movq -64(%rbp), %rdx
    movq (%r10,%rdx,8), %rcx
    testq %rcx, %rcx
    je Ltao_tien_trinh_utf16_giai_phong_xong
    call free
    incq -64(%rbp)
    jmp Ltao_tien_trinh_utf16_giai_phong_tiep
Ltao_tien_trinh_utf16_giai_phong_xong:
    movq -48(%rbp), %rcx
    testq %rcx, %rcx
    je Ltao_tien_trinh_utf16_giai_phong_bang
    call free
Ltao_tien_trinh_utf16_giai_phong_bang:
    movq -40(%rbp), %rcx
    call free
    movq -80(%rbp), %rax
    cmpq $-1, %rax
    je Lbo_nho_loi
    testq %rax, %rax
    je Lbo_nho_loi
    movq %rax, vpp_native_spawn_handle(%rip)
    movl $1, %eax
    TRA_BO_NHO

.globl vpp_native_os_waitpid
vpp_native_os_waitpid:
    VAO_BO_NHO
    cmpl $1, %ecx
    jne Lbo_nho_loi
    KIEM_TRA_LECH %edx, 4
    cmpq $0, vpp_native_spawn_handle(%rip)
    je Lbo_nho_loi
    movl %edx, -24(%rbp)
    leaq -40(%rbp), %rcx       // int *termstat
    movq vpp_native_spawn_handle(%rip), %rdx
    xorl %r8d, %r8d            // _WAIT_CHILD
    call _cwait
    cmpq $-1, %rax
    je Lbo_nho_loi
    movq $0, vpp_native_spawn_handle(%rip)
    movl -40(%rbp), %eax
    andl $255, %eax
    shll $8, %eax              // Quy ước WIFEXITED/WEXITSTATUS
    movl %eax, -44(%rbp)
    call vpp_native_memory_base
    testq %rax, %rax
    je Lbo_nho_loi
    movl -24(%rbp), %edx
    movl -44(%rbp), %ecx
    movl %ecx, (%rax,%rdx)
    movl $1, %eax
    TRA_BO_NHO
