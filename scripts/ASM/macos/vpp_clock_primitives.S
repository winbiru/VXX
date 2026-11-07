// SDK Darwin không công khai số syscall clock_gettime. Cầu ASM gọi API libSystem được hỗ trợ; vẫn
// cần liên kết động libSystem cho đến khi có nguồn đồng hồ độc lập.
#if !defined(__APPLE__)
#error Cầu đồng hồ chỉ hỗ trợ Darwin
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
#error Kiến trúc đồng hồ Darwin chưa được hỗ trợ
#endif
