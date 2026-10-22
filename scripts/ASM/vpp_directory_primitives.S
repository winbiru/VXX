// Darwin directory stream bridge. Linux directory iteration uses
// openat/getdents64/close through vpp_file_primitives.S, with the bounded
// getdents64 record decoder in directory_machine.cpp.
#if defined(__APPLE__)
#define VPP_DIR_OPEN _vpp_machine_dir_open
#define VPP_DIR_NEXT _vpp_machine_dir_next
#define VPP_DIR_CLOSE _vpp_machine_dir_close
#define HOST_DIR_OPEN _opendir
#define HOST_DIR_NEXT _readdir
#define HOST_DIR_CLOSE _closedir
#elif !defined(__linux__)
#error Unsupported directory machine architecture
#endif
#if defined(__APPLE__)
.text
.globl VPP_DIR_OPEN
.globl VPP_DIR_NEXT
.globl VPP_DIR_CLOSE
.p2align 2
VPP_DIR_OPEN:
#if defined(__aarch64__)
    b HOST_DIR_OPEN
#else
    jmp HOST_DIR_OPEN
#endif
.p2align 2
VPP_DIR_NEXT:
#if defined(__aarch64__)
    b HOST_DIR_NEXT
#else
    jmp HOST_DIR_NEXT
#endif
.p2align 2
VPP_DIR_CLOSE:
#if defined(__aarch64__)
    b HOST_DIR_CLOSE
#else
    jmp HOST_DIR_CLOSE
#endif
#endif
#if defined(__linux__)
#if defined(__aarch64__)
.section .note.GNU-stack,"",%progbits
#else
.section .note.GNU-stack,"",@progbits
#endif
#endif
