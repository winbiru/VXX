// getenv is process-local state; the kernel offers no getenv syscall.
// This ASM bridge calls the documented libc API without a C/C++ shim.
// A fully independent bootstrap must capture the process environment at entry.
#if defined(__APPLE__)
#define VPP_ENV_GET _vpp_machine_env_getenv
#define HOST_ENV_GET _getenv
#elif defined(__linux__)
#define VPP_ENV_GET vpp_machine_env_getenv
#define HOST_ENV_GET getenv
#else
#error Unsupported environment machine architecture
#endif
.text
.globl VPP_ENV_GET
.p2align 2
VPP_ENV_GET:
#if defined(__aarch64__)
    b HOST_ENV_GET
#elif defined(__x86_64__) && defined(__APPLE__)
    jmp HOST_ENV_GET
#elif defined(__x86_64__) && defined(__linux__)
    jmp getenv@PLT
#else
#error Unsupported environment machine architecture
#endif
#if defined(__linux__)
#if defined(__aarch64__)
.section .note.GNU-stack,"",%progbits
#else
.section .note.GNU-stack,"",@progbits
#endif
#endif
