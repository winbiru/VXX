// getentropy trên Darwin là API libSystem được hỗ trợ. Điểm vào ASM này vẫn liên kết động với
// libSystem.
#if !defined(__APPLE__)
#error Cầu nguồn ngẫu nhiên chỉ hỗ trợ Darwin
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
#error Kiến trúc nguồn ngẫu nhiên Darwin chưa được hỗ trợ
#endif
