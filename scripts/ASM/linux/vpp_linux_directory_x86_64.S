// Điểm vào Linux cho thao tác thư mục và định danh V++ dùng chung. Nhánh x86-64 gọi syscall trực
// tiếp.
#if !defined(__linux__) || !defined(__x86_64__)
#error Bộ thao tác thư mục độc lập chỉ hỗ trợ Linux x86-64
#endif
#include "../common/posix/vpp_vm_directory_primitives.inc"
.section .note.GNU-stack,"",@progbits
