// Test the same production machine code without linking the guest runtime.
#include "aot_arm64/bo_nho.inc"
#include "aot_arm64/worker.inc"
.zerofill __DATA,__bss,_vpp_native_arena_base,8,3
