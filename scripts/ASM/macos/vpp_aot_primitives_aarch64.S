// Ranh giới máy macOS ARM64 cho runtime.vi. Vùng nhớ mmap có 64 ô 2 MiB, siêu dữ liệu và bảng argv
// riêng cho trình liên kết.
#ifndef VPP_NATIVE_RUNTIME_STACK_ABI
#define VPP_NATIVE_RUNTIME_STACK_ABI 0
#endif

#include "../common/vpp_vietnamese_runtime_symbols.inc"

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
#include "aot_arm64/doi_so.inc"
#include "aot_arm64/xuat_ham.inc"
#include "aot_arm64/may_ao.inc"
