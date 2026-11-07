// Chuyển đối số giữa ABI công khai và hàm thực thi V++.

.macro RT_EXPORT cong_khai, dich, so_doi_so
    .globl \cong_khai
\cong_khai:
#if VPP_NATIVE_RUNTIME_STACK_ABI
    sub sp, sp, #32
    str x30, [sp, #16]
    .if \so_doi_so > 0
    str w0, [sp]
    .endif
    .if \so_doi_so > 1
    str w1, [sp, #4]
    .endif
    .if \so_doi_so > 2
    str w2, [sp, #8]
    .endif
    bl \dich
    ldr x30, [sp, #16]
    add sp, sp, #32
    ret
#else
    b \dich
#endif
.endm
#include "../../common/vpp_aot_runtime_exports.inc"

