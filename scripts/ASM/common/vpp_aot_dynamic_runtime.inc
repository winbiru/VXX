// ABI AOT cho đầy đủ ngôn ngữ. Bộ mã hóa máy hiện có sinh thuật toán; bộ chuyển đổi bổ sung bảng
// xuất hàm của nền tảng.
#if defined(VPP_AOT_VPP_RUNTIME) && VPP_AOT_VPP_RUNTIME
#include "vpp_aot_vpp_runtime.inc"
#else
#if defined(__APPLE__)
#define AOT_PUBLIC(x) _##x
#else
#define AOT_PUBLIC(x) x
#endif

#if defined(__x86_64__) || defined(_M_X64)
.att_syntax prefix
#endif
.text
.p2align 2
#if defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))
// CRT phải cung cấp argv UTF-16 trước khi chuyển qua bảng mã ANSI.
.globl wmain
wmain:
#else
.globl AOT_PUBLIC(main)
AOT_PUBLIC(main):
#endif
#if defined(__aarch64__) || defined(__arm64__)
#if defined(__APPLE__)
    adrp x2, AOT_PUBLIC(vpp_aot_spec)@PAGE
    add x2, x2, AOT_PUBLIC(vpp_aot_spec)@PAGEOFF
#else
    adrp x2, AOT_PUBLIC(vpp_aot_spec)
    add x2, x2, :lo12:AOT_PUBLIC(vpp_aot_spec)
#endif
    b AOT_PUBLIC(vpp_aot_main)
#elif defined(_WIN32)
    leaq AOT_PUBLIC(vpp_aot_spec)(%rip), %r8
    jmp AOT_PUBLIC(vpp_aot_wmain)
#else
    leaq AOT_PUBLIC(vpp_aot_spec)(%rip), %rdx
    jmp AOT_PUBLIC(vpp_aot_main)
#endif

.macro DYNAMIC_EXPORT cong_khai, dich
    .globl \cong_khai
\cong_khai:
#if defined(__aarch64__) || defined(__arm64__)
    b \dich
#else
    jmp \dich
#endif
.endm
#include "vpp_aot_runtime_exports.inc"

#if defined(__linux__)
.section .note.GNU-stack,"",@progbits
#endif
#undef AOT_PUBLIC
#endif
