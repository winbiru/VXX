// Full-language AOT ABI. Algorithmic code is emitted by the existing machine
// encoders; these adapters extend the platform export table without a VM.
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
.globl AOT_PUBLIC(main)
AOT_PUBLIC(main):
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
    jmp AOT_PUBLIC(vpp_aot_main)
#else
    leaq AOT_PUBLIC(vpp_aot_spec)(%rip), %rdx
    jmp AOT_PUBLIC(vpp_aot_main)
#endif

.macro DYNAMIC_EXPORT public, target
    .globl \public
\public:
#if defined(__aarch64__) || defined(__arm64__)
    b \target
#else
    jmp \target
#endif
.endm
#include "vpp_aot_runtime_exports.inc"

#if defined(__linux__)
.section .note.GNU-stack,"",@progbits
#endif
#undef AOT_PUBLIC
