// Cầu nối luồng thư mục Darwin. Linux dùng openat/getdents64/close trong vpp_file_primitives.S;
// directory_machine.cpp giải mã bản ghi có kiểm tra giới hạn.
#if defined(__APPLE__)
#define VPP_DIR_OPEN _vpp_machine_dir_open
#define VPP_DIR_NEXT _vpp_machine_dir_next
#define VPP_DIR_CLOSE _vpp_machine_dir_close
#define HOST_DIR_OPEN _opendir
#define HOST_DIR_NEXT _readdir
#define HOST_DIR_CLOSE _closedir
#elif !defined(__linux__)
#error Kiến trúc máy thư mục chưa được hỗ trợ
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
