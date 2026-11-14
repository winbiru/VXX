// Win64 OS boundary and V++ runtime exports. All algorithms remain in .vi.
#if !defined(_WIN32) || !defined(__x86_64__)
#error Windows x86-64 required
#endif
#define VPP_WINDOWS_BOUNDARY_TEST 1
#include "vpp_aot_primitives_windows_x86_64.S"
.purgem RT_EXPORT
.text
.p2align 4
#include "../linux/vpp_linux_aot_runtime_symbols.inc"
.macro RT_EXPORT public, target, count
    .globl \public
\public:
    jmp "\target"
.endm
#include "../linux/vpp_linux_aot_runtime_exports.inc"
RT_EXPORT vpp_native_f64_parse, vpp_fn_vm_f64_parse, 2
RT_EXPORT vpp_native_f64_text, vpp_fn_vm_f64_text, 2
#define VPP_VM_F64_FREESTANDING 1
#include "../common/posix/vpp_vm_f64_primitives.inc"
#include "aot_x86_64/he_thong.inc"
#include "aot_x86_64/thu_muc.inc"
#include "aot_x86_64/tien_trinh_day_du.inc"
#include "aot_x86_64/o_cam.inc"
