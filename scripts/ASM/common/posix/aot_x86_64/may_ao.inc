// Chèn các phép toán máy dùng chung cho VM.

#if !defined(__APPLE__) && !VPP_NATIVE_RUNTIME_STACK_ABI
#include "../../vpp_vietnamese_runtime_symbols.inc"

#endif

#include "../vpp_vm_f64_primitives.inc"
#include "../vpp_vm_system_primitives.inc"
#include "../vpp_vm_directory_primitives.inc"
#if defined(__linux__)
// Giữ môi trường ban đầu từ nhân qua hàm lấy envp của tệp này.
#define VPP_PROCESS_HOST_INITIAL_ENVP 1
#endif
#include "../vpp_vm_process_primitives.inc"
#if defined(__linux__)
#undef VPP_PROCESS_HOST_INITIAL_ENVP
#endif
#include "../vpp_vm_socket_primitives.inc"
