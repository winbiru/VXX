// Linux ARM64 machine primitives for the Native AOT Stage 0 C bootstrap.
// Argument and high-level runtime exports come from vpp_aot_runtime.c.
#define VPP_NATIVE_RUNTIME_STACK_ABI 0

#include "aot_arm64/bo_nho.inc"
#include "aot_arm64/tep.inc"
#include "aot_arm64/tien_trinh.inc"
#include "aot_arm64/thong_tin_tep.inc"
#include "aot_arm64/moi_truong.inc"
#include "aot_arm64/duong_dan.inc"
#include "aot_arm64/sha256.inc"
#include "aot_arm64/bao_cao.inc"
#include "aot_arm64/so_nguyen.inc"

// Local constants and arena state used by the machine primitives above.
.section .rodata
Lthu_muc_nha_khoa: .asciz "VPP_HOME"
Lchuong_trinh_duong: .asciz "/proc/self/exe"
Lclang_duong: .asciz "/usr/bin/clang"
.bss
.p2align 3
vpp_native_arena_base: .skip 8
.section .note.GNU-stack,"",@progbits
