// Darwin has no public clock_gettime syscall number in its SDK.
// This assembly-only OS API boundary calls the supported libSystem symbol.
// It remains dynamically linked to libSystem until an independent clock
// boundary is available; no C/C++ function acts as a bridge.
#if !defined(__APPLE__)
#error Darwin clock bridge only
#endif
.text
.globl _vpp_machine_clock_gettime
.p2align 2
_vpp_machine_clock_gettime:
#if defined(__aarch64__)
    b _clock_gettime
#elif defined(__x86_64__)
    jmp _clock_gettime
#else
#error Unsupported Darwin clock architecture
#endif
