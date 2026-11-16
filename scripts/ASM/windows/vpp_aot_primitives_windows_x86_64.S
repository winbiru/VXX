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
#ifdef VPP_WINDOWS_I32_BOOTSTRAP
#if !VPP_NATIVE_RUNTIME_STACK_ABI || !defined(VPP_JIT_DISABLED) || defined(VPP_STANDALONE_VM_WORKER)
#error VPP_WINDOWS_I32_BOOTSTRAP requires the restricted i32 PE boundary
#endif
// The pinned i32 linker has no worker imports. Report unsupported, never
// simulate parallel execution. Full AOT/raw/standalone VM use worker.inc.
.text
.globl vpp_native_worker_service
vpp_native_worker_service:
    xorl %eax, %eax
    testl %ecx, %ecx
    je 1f
    movl $-1, %eax
1:  ret
#else
#include "aot_x86_64/worker.inc"
#endif
#ifdef VPP_JIT_DISABLED
.text
.globl vpp_native_jit_service
vpp_native_jit_service:
    xorl %eax, %eax
    testl %ecx, %ecx
    jz 1f
    movl $-3, %eax
1:  ret
.globl vpp_native_jit_image
vpp_native_jit_image:
    xorl %eax, %eax
    xorl %edx, %edx
    ret
#else
#include "../common/vpp_jit_x86_64.inc"
#endif
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
