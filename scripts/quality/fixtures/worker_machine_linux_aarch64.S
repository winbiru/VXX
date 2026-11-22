// Native Linux ARM64 worker test using production memory/worker includes.
#include "../../ASM/linux/aot_arm64/bo_nho.inc"
#include "../../ASM/linux/aot_arm64/tep.inc"
#include "../../ASM/linux/aot_arm64/thong_tin_tep.inc"
#include "../../ASM/linux/aot_arm64/duong_dan.inc"
#include "../../ASM/linux/aot_arm64/moi_truong.inc"
#include "../../ASM/linux/aot_arm64/tien_trinh.inc"
#include "../../ASM/linux/aot_arm64/worker.inc"

.section .rodata
Lthu_muc_nha_khoa: .asciz "VPP_HOME"
Lchuong_trinh_duong: .asciz "/proc/self/exe"
Lclang_duong: .asciz "/usr/bin/clang"

.bss
.p2align 3
vpp_native_arena_base: .skip 8
.p2align 2
vpp_native_arena_owner_tid: .skip 4
vpp_native_arena_tls_key: .skip 8
.p2align 2
vpp_native_arena_tls_enabled: .skip 4
.p2align 2
vpp_native_arena_tls_lock: .skip 4
.p2align 3
vpp_native_worker_tls_key: .skip 8
.section .note.GNU-stack,"",@progbits
