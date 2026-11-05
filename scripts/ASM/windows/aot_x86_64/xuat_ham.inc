// Chuyển đối số giữa ABI công khai và hàm thực thi V++.

// Bộ chuyển đổi ABI Microsoft x64. ABI ngăn xếp V++ đặt đối số sau vùng đối số dự phòng bắt buộc 32
// byte.
.macro RT_EXPORT cong_khai, dich, so_doi_so
    .globl \cong_khai
\cong_khai:
#if VPP_NATIVE_RUNTIME_STACK_ABI
    subq $(56), %rsp
    .if \so_doi_so > 0
    movl %ecx, 32(%rsp)
    .endif
    .if \so_doi_so > 1
    movl %edx, 36(%rsp)
    .endif
    .if \so_doi_so > 2
    movl %r8d, 40(%rsp)
    .endif
    call \dich
    addq $(56), %rsp
    ret
#else
    jmp \dich
#endif
.endm
#ifndef VPP_WINDOWS_BOUNDARY_TEST
#include "../../common/vpp_aot_runtime_exports.inc"
#endif

