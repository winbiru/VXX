// Ranh giới ABI giữa AOT Linux x86-64 và bộ thực thi V++ độc lập. Điểm _start nằm trong
// vpp_linux_start_x86_64.S.
#if !defined(__linux__) || !defined(__x86_64__)
#error Cầu AOT V++ độc lập yêu cầu Linux x86-64
#endif
#define VPP_NATIVE_RUNTIME_STACK_ABI 1
#include "vpp_linux_aot_runtime_symbols.inc"

.text
.p2align 4
// Gọi main của bộ thực thi V++; căn ngăn xếp 16 byte trước lời gọi theo SysV AMD64. Kết quả dùng
// i32.
.globl vpp_freestanding_main
.type vpp_freestanding_main, @function
vpp_freestanding_main:
    pushq %rbp
    movq %rsp, %rbp
    subq $16, %rsp
    call main
    movl %eax, -4(%rbp)
    cmpl $0, vpp_linux_cache_report_requested(%rip)
    je .Ltu_chu_da_xong
    call vpp_native_cache_report_entry
.Ltu_chu_da_xong:
    movl -4(%rbp), %eax
    leave
    ret
.size vpp_freestanding_main, .-vpp_freestanding_main

// Bộ sinh i32 đặt đối số trong ô 4 byte trên ngăn xếp bên gọi. Khi vào hàm SysV, rsp%16=8; dành 40
// byte để căn lời gọi tiếp theo.
.macro RT_EXPORT cong_khai, dich, so_doi_so
    .globl \cong_khai
    .type \cong_khai, @function
\cong_khai:
    subq $40, %rsp
    .if \so_doi_so > 0
    movl %edi, (%rsp)
    .endif
    .if \so_doi_so > 1
    movl %esi, 4(%rsp)
    .endif
    .if \so_doi_so > 2
    movl %edx, 8(%rsp)
    .endif
// Bộ biên dịch ELF i32 sinh tên UTF-8 chứa dấu cách. Bọc lại đối số bằng dấu nháy vì GAS bỏ dấu nháy
// khi khai triển macro xuất hàm.
    call "\dich"
    addq $40, %rsp
    ret
.endm
#include "vpp_linux_aot_runtime_exports.inc"
// Chuyển số thập phân trong đối tượng V++ thay cho strtod/snprintf.
RT_EXPORT vpp_native_f64_parse, vpp_fn_vm_f64_parse, 2
RT_EXPORT vpp_native_f64_text, vpp_fn_vm_f64_text, 2

.globl vpp_native_i32_div
.type vpp_native_i32_div, @function
vpp_native_i32_div:
    testl %esi, %esi
    je .Lchia_khong
    cmpl $-1, %esi
    jne .Lchia_tep_thuong
    cmpl $0x80000000, %edi
    je .Lchia_tran
.Lchia_tep_thuong:
    movl %edi, %eax
    cltd
    idivl %esi
    ret
.Lchia_khong:
    movl $-1, %eax
    ret
.Lchia_tran:
    movl $0x80000000, %eax
    ret

// Chạy báo cáo bộ nhớ đệm sau khi main của bộ thực thi thoát bình thường.
.globl vpp_native_os_register_cache_report
.type vpp_native_os_register_cache_report, @function
vpp_native_os_register_cache_report:
    movl $1, vpp_linux_cache_report_requested(%rip)
    xorl %eax, %eax
    ret

.bss
.p2align 2
vpp_linux_cache_report_requested: .zero 4
.section .note.GNU-stack,"",@progbits
