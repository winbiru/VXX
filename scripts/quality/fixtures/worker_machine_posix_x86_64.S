// Test production SysV memory/worker code without linking the guest runtime.
#define VPP_ARENA_SIZE 135271424
#define VPP_PATH_MAX 4096
#ifdef __APPLE__
#define PUBLIC(x) _##x
#define LIB(x) _##x
#else
#define PUBLIC(x) x
#define LIB(x) x@PLT
#endif
#include "aot_x86_64/bo_nho.inc"
#include "aot_x86_64/worker.inc"
.data
.p2align 3
PUBLIC(vpp_native_arena_base): .quad 0
#ifndef __APPLE__
.section .note.GNU-stack,"",@progbits
#endif
