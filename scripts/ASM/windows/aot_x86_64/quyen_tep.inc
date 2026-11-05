// Điểm vào đặt quyền tệp trên nền tảng hiện tại.

// Thao tác đặt quyền thực thi POSIX chưa hỗ trợ trên Windows: trả lỗi.
.macro CHUA_HO_TRO ten
    .globl \ten
\ten:
    movl $(-1), %eax
    ret
.endm
// Bộ phân tích nhận phép toán máy 78 trên mọi đích. Windows không có bit thực thi POSIX; trả lỗi cho
// đến khi đường xuất PE/COFF xử lý rõ quy ước quyền này.
CHUA_HO_TRO vpp_native_os_fchmod
