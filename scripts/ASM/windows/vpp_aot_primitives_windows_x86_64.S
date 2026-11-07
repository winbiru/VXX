// Windows x64: COFF, ABI Microsoft x64, MSVC/UCRT và kernel32. ASM cung cấp bộ nhớ, nhập xuất, đối
// số, đường dẫn, tạo/đợi tiến trình qua UCRT và SHA-256 qua CNG. V++ liên kết cùng bộ thực thi và tự
// ghi bảng nhập PE. Hợp dịch: clang -target x86_64-pc-windows-msvc -c <tệp.S>.
#ifndef VPP_NATIVE_RUNTIME_STACK_ABI
#define VPP_NATIVE_RUNTIME_STACK_ABI 0
#endif
#define PUBLIC(x) x
#if VPP_NATIVE_RUNTIME_STACK_ABI
#define RT_TARGET(x) PUBLIC(vpp_fn_##x)
#else
#define RT_TARGET(x) PUBLIC(x)
#endif
#define VPP_ARENA_SIZE 135271424

#include "aot_x86_64/bo_nho.inc"
#include "aot_x86_64/nhap_xuat.inc"
#include "aot_x86_64/unicode.inc"
#include "aot_x86_64/trich_doi_so.inc"
#include "aot_x86_64/tep.inc"
#include "aot_x86_64/thong_tin_tep.inc"
#include "aot_x86_64/moi_truong.inc"
#include "aot_x86_64/duong_dan.inc"
#ifndef VPP_WINDOWS_STAGE0
// Stage 0 owns argv as C byte handles, including UTF-16 -> UTF-8 conversion.
#include "aot_x86_64/doi_so.inc"
#endif
#include "aot_x86_64/so_nguyen.inc"
#include "aot_x86_64/tien_trinh.inc"
#include "aot_x86_64/sha256.inc"
#include "aot_x86_64/quyen_tep.inc"
#include "aot_x86_64/bao_cao.inc"
#include "aot_x86_64/xuat_ham.inc"
#include "aot_x86_64/du_lieu.inc"
