// Primitive máy cho trình liên kết AOT Stage 0. Nhóm byte/tệp cấp cao do
// vpp_aot_runtime.c bootstrap cung cấp; tệp này chỉ chứa ranh giới OS/arena.
#define VPP_NATIVE_RUNTIME_STACK_ABI 0

#include "aot_arm64/bo_nho.inc"
#include "aot_arm64/tep.inc"
#include "aot_arm64/tien_trinh.inc"
#include "aot_arm64/thong_tin_tep.inc"
#include "aot_arm64/tep_theo_duong.inc"
#include "aot_arm64/moi_truong.inc"
#include "aot_arm64/sha256.inc"
#include "aot_arm64/bao_cao.inc"
#include "aot_arm64/duong_dan.inc"
#include "aot_arm64/so_nguyen.inc"

// bo_nho.inc dùng con trỏ arena này. Bản self-host đầy đủ khai báo nó trong
// xuat_ham.inc; Stage 0 cố ý không include xuat_ham để tránh trùng symbol.
.zerofill __DATA,__bss,_vpp_native_arena_base,8,3
.subsections_via_symbols
