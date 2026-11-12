// Môi trường thuộc trạng thái tiến trình nên nhân không có syscall getenv. Cầu ASM gọi API libc;
// đường khởi động độc lập phải giữ môi trường ngay tại điểm vào.
#if defined(__APPLE__)
#define VPP_ENV_GET _vpp_machine_env_getenv
#define HOST_ENV_GET _getenv
#elif defined(__linux__)
#define VPP_ENV_GET vpp_machine_env_getenv
#define HOST_ENV_GET getenv
#else
#error Kiến trúc máy môi trường chưa được hỗ trợ
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
#error Kiến trúc máy môi trường chưa được hỗ trợ
#endif
#if defined(__linux__)
#if defined(__aarch64__)
.section .note.GNU-stack,"",%progbits
#else
.section .note.GNU-stack,"",@progbits
#endif
#endif
