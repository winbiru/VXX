// localtime has no kernel syscall: use the public reentrant OS library ABI.
// This bridge will eventually be replaced by native timezone parsing in .vi.
#if defined(__APPLE__)
#define VPP_LOCAL _vpp_machine_localtime_r
#define HOST_LOCAL _localtime_r
#elif defined(__linux__)
#define VPP_LOCAL vpp_machine_localtime_r
#define HOST_LOCAL localtime_r
#else
#error Unsupported localtime machine architecture
#endif
.text
.globl VPP_LOCAL
.p2align 2
VPP_LOCAL:
#if defined(__aarch64__)
    b HOST_LOCAL
#elif defined(__x86_64__) && defined(__APPLE__)
    jmp HOST_LOCAL
#elif defined(__x86_64__) && defined(__linux__)
    jmp localtime_r@PLT
#else
#error Unsupported localtime machine architecture
#endif
#if defined(__linux__)
#if defined(__aarch64__)
.section .note.GNU-stack,"",%progbits
#else
.section .note.GNU-stack,"",@progbits
#endif
#endif
