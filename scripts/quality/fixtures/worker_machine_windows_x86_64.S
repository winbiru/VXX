// Exercise the production Windows worker and memory boundary in a test DLL.
#define VPP_ARENA_SIZE 135271424
#include "aot_x86_64/bo_nho.inc"
#include "aot_x86_64/worker.inc"
.data
.p2align 3
vpp_native_arena_base: .quad 0
vpp_native_arena_tls_key: .long 0
vpp_native_arena_tls_enabled: .long 0
