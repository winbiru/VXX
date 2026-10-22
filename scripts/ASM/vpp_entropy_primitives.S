// On Darwin, getentropy() is a supported libSystem API, not a public syscall.
// This entry point is an assembly-only bridge; dynamic libSystem remains.
#if !defined(__APPLE__)
#error Darwin entropy bridge only
#endif
.text
.globl _vpp_machine_entropy_getentropy
.p2align 2
_vpp_machine_entropy_getentropy:
#if defined(__aarch64__)
    b _getentropy
#elif defined(__x86_64__)
    jmp _getentropy
#else
#error Unsupported Darwin entropy architecture
#endif
