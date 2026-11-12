// Machine primitives for the Linux x86-64 C bootstrap. The bootstrap runtime
// supplies the high-level byte/file/argument functions; only the OS boundary,
// memory arena and integer helpers belong in this object.
#define VPP_NATIVE_RUNTIME_STACK_ABI 0
#define PUBLIC(x) x
#define LIB(x) x@PLT
#define VPP_ARENA_SIZE 135271424
#define VPP_PATH_MAX 4096

#include "../common/posix/aot_x86_64/bo_nho.inc"
#include "../common/posix/aot_x86_64/nhap_xuat.inc"
#include "../common/posix/aot_x86_64/tep.inc"
#include "../common/posix/aot_x86_64/tien_trinh.inc"
#include "../common/posix/aot_x86_64/thong_tin_tep.inc"
#include "../common/posix/aot_x86_64/moi_truong.inc"
#include "../common/posix/aot_x86_64/duong_dan.inc"
#include "../common/posix/aot_x86_64/sha256.inc"
#include "../common/posix/aot_x86_64/bao_cao.inc"
#include "../common/posix/aot_x86_64/so_nguyen.inc"
#include "../common/posix/vpp_vm_f64_primitives.inc"
#include "../common/posix/aot_x86_64/du_lieu.inc"
#include "vpp_linux_sha256_x86_64.S"
#include "vpp_linux_syscalls_x86_64.S"
