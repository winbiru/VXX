// Ranh giới máy POSIX x86-64 cho runtime.vi, dùng cùng quy ước điểm vào công khai với ARM64.
// Bố trí vùng nhớ phụ thuộc nhánh hệ điều hành; con trỏ giữ đủ 64 bit, mã tài nguyên dùng i32.
#ifndef VPP_NATIVE_RUNTIME_STACK_ABI
#if defined(__linux__) && defined(VPP_LINUX_HOST_NO_CRT)
#define VPP_NATIVE_RUNTIME_STACK_ABI 1
#else
#define VPP_NATIVE_RUNTIME_STACK_ABI 0
#endif
#endif
#ifdef __APPLE__
#define PUBLIC(x) _##x
#define LIB(x) _##x
#else
#define PUBLIC(x) x
#define LIB(x) x@PLT
#endif
#ifdef __APPLE__
#include "../vpp_vietnamese_runtime_symbols.inc"
#define RT_TARGET(x) x
#elif VPP_NATIVE_RUNTIME_STACK_ABI && defined(__linux__)
// Bộ biên dịch ELF i32 xuất vpp_fn_<tên UTF-8> với đối số i32 xếp sát trên ngăn xếp. Các bí danh
// vpp_u_<hex> của ABI cũ không có trong đối tượng tự biên dịch.
#include "../../linux/vpp_linux_aot_runtime_symbols.inc"
#define RT_TARGET(x) x
#elif VPP_NATIVE_RUNTIME_STACK_ABI
#define RT_TARGET(x) PUBLIC(vpp_fn_##x)
#else
#define RT_TARGET(x) PUBLIC(x)
#endif
#define VPP_ARENA_SIZE 135271424
#define VPP_PATH_MAX 4096

#include "aot_x86_64/bo_nho.inc"
#include "aot_x86_64/nhap_xuat.inc"
#include "aot_x86_64/tep.inc"
#include "aot_x86_64/tien_trinh.inc"
#include "aot_x86_64/thong_tin_tep.inc"
#include "aot_x86_64/moi_truong.inc"
#include "aot_x86_64/duong_dan.inc"
#include "aot_x86_64/sha256.inc"
#include "aot_x86_64/bao_cao.inc"
#include "aot_x86_64/so_nguyen.inc"
#include "aot_x86_64/doi_so.inc"
#include "aot_x86_64/xuat_ham.inc"
#include "aot_x86_64/du_lieu.inc"
#include "aot_x86_64/khoi_dong.inc"
#include "aot_x86_64/may_ao.inc"
