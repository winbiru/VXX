// Linux ARM64: ELF, AAPCS64, glibc và OpenSSL libcrypto. Con trỏ vùng nhớ giữ đủ 64 bit. Hợp dịch:
// clang -target aarch64-unknown-linux-gnu -c <tệp.S>.
#ifndef VPP_NATIVE_RUNTIME_STACK_ABI
#define VPP_NATIVE_RUNTIME_STACK_ABI 0
#endif
#define PUBLIC(x) x
#if VPP_NATIVE_RUNTIME_STACK_ABI
#define RT_TARGET(x) PUBLIC(vpp_fn_##x)
#else
#define RT_TARGET(x) PUBLIC(x)
#endif

#include "aot_arm64/bo_nho.inc"
#include "aot_arm64/tep.inc"
#include "aot_arm64/tien_trinh.inc"
#include "aot_arm64/thong_tin_tep.inc"
#include "aot_arm64/moi_truong.inc"
#include "aot_arm64/duong_dan.inc"
#include "aot_arm64/sha256.inc"
#include "aot_arm64/bao_cao.inc"
#include "aot_arm64/so_nguyen.inc"
#include "aot_arm64/doi_so.inc"
#include "aot_arm64/xuat_ham.inc"
#include "aot_arm64/du_lieu.inc"
