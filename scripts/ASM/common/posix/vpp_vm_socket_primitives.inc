// ABI ổ cắm VM: 0 kết nối, 1 lắng nghe, 2 chấp nhận, 3 gửi, 4 nhận, 5 đóng, 6 đặt thời gian chờ, 7
// gắn UDP, 8 nhận gói, 9 gửi gói, 10 lấy cổng cục bộ. V++ quản lý tài nguyên và mô phỏng DNS. Khối
// điều khiển 128 byte: fd@0, loại@4, cổng@8, thời gian chờ@12, độ lệch dữ liệu@16, độ dài@20, độ dài
// IP@24, errno@28, IP@32, sockaddr@48, timeval@80, pollfd@96, cờ cũ@104, lỗi@108, socklen@112. Thời
// hạn đơn điệu và timespec nằm trên ngăn xếp riêng.
#if defined(__aarch64__) || defined(__arm64__)
#ifdef __APPLE__
#define VMSOCK_SYM(x) _##x
#define VMSOCK_LIB(x) _##x
#define VMSOCK_LEVEL 0xffff
#define VMSOCK_REUSE 4
#define VMSOCK_RCVTIMEO 0x1006
#define VMSOCK_SNDTIMEO 0x1005
#define VMSOCK_NOSIGPIPE 0x1022
#define VMSOCK_MSG 0
#define VMSOCK_INET6 30
#define VMSOCK_NONBLOCK 4
#define VMSOCK_INPROGRESS 36
#define VMSOCK_TIMEDOUT 60
#define VMSOCK_SOERROR 0x1007
#define VMSOCK_CLOCK 6
#else
#define VMSOCK_SYM(x) x
#define VMSOCK_LIB(x) x
#define VMSOCK_LEVEL 1
#define VMSOCK_REUSE 2
#define VMSOCK_RCVTIMEO 20
#define VMSOCK_SNDTIMEO 21
#define VMSOCK_MSG 0x4000
#define VMSOCK_INET6 10
#define VMSOCK_NONBLOCK 2048
#define VMSOCK_INPROGRESS 115
#define VMSOCK_TIMEDOUT 110
#define VMSOCK_SOERROR 4
#define VMSOCK_CLOCK 1
#endif
.text
.p2align 2
.globl VMSOCK_SYM(vpp_native_os_socket)
VMSOCK_SYM(vpp_native_os_socket):
    stp x19, x20, [sp, #-96]!
    stp x21, x22, [sp, #16]
    stp x23, x30, [sp, #32]
    mov w19, w0
    mov w21, w1
    bl VMSOCK_SYM(vpp_native_memory_base)
    cbz x0, Lo_cam_sai_a
    cmp w19, #10
    b.hi Lo_cam_sai_a
// Vùng nhớ 68162560 byte; khối điều khiển 128 byte, căn theo 16 byte.
    mov w9, #4992
    movk w9, #1040, lsl #16
    cmp w21, w9
    b.hi Lo_cam_sai_a
    tst w21, #15
    b.ne Lo_cam_sai_a
    add x20, x0, w21, uxtw
    str wzr, [x20, #28]
    cmp w19, #7
    b.eq Lo_cam_udp_gan_a
    cmp w19, #8
    b.eq Lo_cam_nhap_xuat_a
    cmp w19, #9
    b.eq Lo_cam_nhap_xuat_a
    cmp w19, #10
    b.eq Lo_cam_cong_a
    cbz w19, Lo_cam_ket_noi_a
    cmp w19, #1
    b.eq Lo_cam_lang_nghe_a
    cmp w19, #2
    b.eq Lo_cam_chap_nhan_a
    cmp w19, #3
    b.eq Lo_cam_nhap_xuat_a
    cmp w19, #4
    b.eq Lo_cam_nhap_xuat_a
    cmp w19, #5
    b.eq Lo_cam_dong_a
    b Lo_cam_thoi_gian_cho_a
Lo_cam_dia_chi_a:
// w0 giữ AF_INET/AF_INET6; tạo sockaddr trong vùng điều khiển dành riêng.
    mov w22, w0
    stp xzr, xzr, [x20, #48]
    stp xzr, xzr, [x20, #64]
#ifdef __APPLE__
    cmp w22, #2
    b.ne 1f
    mov w9, #16
    b 2f
1:  mov w9, #28
2:  strb w9, [x20, #48]
    strb w22, [x20, #49]
#else
    strh w22, [x20, #48]
#endif
    ldr w9, [x20, #8]
    rev16 w9, w9
    strh w9, [x20, #50]
    cmp w22, #2
    b.ne 3f
    ldr w9, [x20, #32]
    str w9, [x20, #52]
    ret
3:  ldp x9, x10, [x20, #32]
    stp x9, x10, [x20, #56]
    ret
Lo_cam_udp_gan_a:
    ldr w9, [x20, #8]
    cmp w9, #0
    b.lt Lo_cam_sai_a
    mov w10, #65535
    cmp w9, w10
    b.gt Lo_cam_sai_a
    ldr w9, [x20, #12]
    cmp w9, #1
    b.lt Lo_cam_sai_a
    mov w10, #0x93e0
    movk w10, #4, lsl #16
    cmp w9, w10
    b.gt Lo_cam_sai_a
    mov w0, #2
    bl Lo_cam_dia_chi_a
    mov w0, #2
    mov w1, #2
    mov w2, #0
    bl VMSOCK_LIB(socket)
    cmp w0, #0
    b.lt Lo_cam_errno_ve_a
    str w0, [x20]
    bl Lo_cam_dat_thoi_gian_cho_a
    cmp w0, #0
    b.lt Lo_cam_dong_loi_a
    ldr w0, [x20]
    add x1, x20, #48
    mov w2, #16
    bl VMSOCK_LIB(bind)
    cmp w0, #0
    b.lt Lo_cam_errno_dong_a
    ldr w0, [x20]
    b Lo_cam_ve_a
Lo_cam_cong_a:
    ldr w0, [x20]
    cmp w0, #0
    b.lt Lo_cam_sai_a
    mov w9, #32
    str w9, [x20, #112]
    add x1, x20, #48
    add x2, x20, #112
    bl VMSOCK_LIB(getsockname)
    cmp w0, #0
    b.lt Lo_cam_errno_ve_a
    ldrh w0, [x20, #50]
    rev16 w0, w0
    str w0, [x20, #8]
    b Lo_cam_ve_a
Lo_cam_ket_noi_a:
    ldr w9, [x20, #24]
    cmp w9, #4
    b.eq 1f
    cmp w9, #16
    b.ne Lo_cam_sai_a
1:  ldr w9, [x20, #8]
    cmp w9, #1
    b.lt Lo_cam_sai_a
    mov w10, #65535
    cmp w9, w10
    b.gt Lo_cam_sai_a
    ldr w9, [x20, #4]
    cmp w9, #1
    b.hi Lo_cam_sai_a
    mov w0, #2
    ldr w9, [x20, #24]
    cmp w9, #16
    b.ne 2f
    mov w0, #VMSOCK_INET6
2:  bl Lo_cam_dia_chi_a
    mov w0, w22
    mov w1, #1
    ldr w9, [x20, #4]
    cbz w9, 3f
    mov w1, #2
3:  mov w2, #0
    bl VMSOCK_LIB(socket)
    cmp w0, #0
    b.lt Lo_cam_errno_ve_a
    str w0, [x20]
    bl Lo_cam_khong_sigpipe_a
    cmp w0, #0
    b.lt Lo_cam_dong_loi_a
    ldr w9, [x20, #12]
    cmp w9, #1
    b.lt Lo_cam_sai_mo_ta_a
    mov w10, #0x93e0
    movk w10, #4, lsl #16
    cmp w9, w10
    b.gt Lo_cam_sai_mo_ta_a
    bl Lo_cam_dat_thoi_gian_cho_a
    cmp w0, #0
    b.lt Lo_cam_dong_loi_a
// SO_SNDTIMEO không giới hạn connect đang chặn. Tạm bật O_NONBLOCK, dùng poll rồi khôi phục cờ cũ.
    ldr w0, [x20]
    mov w1, #3 // F_GETFL
    bl VMSOCK_LIB(fcntl)
    cmp w0, #0
    b.lt Lo_cam_errno_dong_a
    str w0, [x20, #104]
    orr w2, w0, #VMSOCK_NONBLOCK
    ldr w0, [x20]
    mov w1, #4 // F_SETFL
    bl VMSOCK_LIB(fcntl)
    cmp w0, #0
    b.lt Lo_cam_errno_dong_a
    ldr w0, [x20]
    add x1, x20, #48
    mov w2, #16
    ldr w9, [x20, #24]
    cmp w9, #16
    b.ne 4f
    mov w2, #28
4:  bl VMSOCK_LIB(connect)
    cmp w0, #0
    b.ge Lo_cam_da_noi_a
    bl Lo_cam_errno_a
    mov w9, #-VMSOCK_INPROGRESS
    cmp w0, w9
    b.eq Lo_cam_ket_noi_cho_a
    mov w9, #-4 // EINTR khi kết nối không chặn cũng có nghĩa đang chờ
    cmp w0, w9
    b.ne Lo_cam_ket_noi_khoi_phuc_loi_a
Lo_cam_ket_noi_cho_a:
    ldr w9, [x20]
    str w9, [x20, #96]
    mov w9, #4 // POLLOUT
    strh w9, [x20, #100]
    strh wzr, [x20, #102]
    bl Lo_cam_han_khoi_dong_a
    cmp w0, #0
    b.lt Lo_cam_ket_noi_khoi_phuc_loi_a
Lo_cam_ket_noi_tham_do_lap_a:
    bl Lo_cam_han_con_lai_a
    cmp w0, #0
    b.lt Lo_cam_ket_noi_khoi_phuc_loi_a
    b.eq Lo_cam_ket_noi_qua_han_a
    mov w2, w0
    add x0, x20, #96
    mov w1, #1
    bl VMSOCK_LIB(poll)
    cmp w0, #0
    b.lt Lo_cam_ket_noi_tham_do_loi_a
    b.eq Lo_cam_ket_noi_qua_han_a
    ldr w0, [x20]
    mov w1, #VMSOCK_LEVEL
    mov w2, #VMSOCK_SOERROR
    add x3, x20, #108
    mov w9, #4
    str w9, [x20, #112]
    add x4, x20, #112
    bl VMSOCK_LIB(getsockopt)
    cmp w0, #0
    b.lt Lo_cam_ket_noi_tham_do_loi_a
    ldr w9, [x20, #108]
    cbz w9, Lo_cam_da_noi_a
    str w9, [x20, #28]
    neg w0, w9
    b Lo_cam_ket_noi_khoi_phuc_loi_a
Lo_cam_ket_noi_qua_han_a:
    mov w9, #VMSOCK_TIMEDOUT
    str w9, [x20, #28]
    neg w0, w9
    b Lo_cam_ket_noi_khoi_phuc_loi_a
Lo_cam_ket_noi_tham_do_loi_a:
    bl Lo_cam_errno_a
    mov w9, #-4 // EINTR
    cmp w0, w9
    b.eq Lo_cam_ket_noi_tham_do_lap_a
Lo_cam_ket_noi_khoi_phuc_loi_a:
    mov w23, w0
    ldr w0, [x20]
    mov w1, #4
    ldr w2, [x20, #104]
    bl VMSOCK_LIB(fcntl)
    mov w0, w23
    b Lo_cam_dong_loi_a
Lo_cam_da_noi_a:
    ldr w0, [x20]
    mov w1, #4
    ldr w2, [x20, #104]
    bl VMSOCK_LIB(fcntl)
    cmp w0, #0
    b.lt Lo_cam_errno_dong_a
    ldr w0, [x20]
    b Lo_cam_ve_a
Lo_cam_lang_nghe_a:
    ldr w9, [x20, #8]
    cmp w9, #1
    b.lt Lo_cam_sai_a
    mov w10, #65535
    cmp w9, w10
    b.gt Lo_cam_sai_a
    ldr w9, [x20, #20]
    cmp w9, #1
    b.lt Lo_cam_sai_a
    cmp w9, #1024
    b.gt Lo_cam_sai_a
    mov w0, #2
    bl Lo_cam_dia_chi_a
    mov w0, #2
    mov w1, #1
    mov w2, #0
    bl VMSOCK_LIB(socket)
    cmp w0, #0
    b.lt Lo_cam_errno_ve_a
    str w0, [x20]
    mov w9, #1
    str w9, [x20, #96]
    ldr w0, [x20]
    mov w1, #VMSOCK_LEVEL
    mov w2, #VMSOCK_REUSE
    add x3, x20, #96
    mov w4, #4
    bl VMSOCK_LIB(setsockopt)
    cmp w0, #0
    b.lt Lo_cam_errno_dong_a
    ldr w0, [x20]
    add x1, x20, #48
    mov w2, #16
    bl VMSOCK_LIB(bind)
    cmp w0, #0
    b.lt Lo_cam_errno_dong_a
    ldr w0, [x20]
    ldr w1, [x20, #20]
    bl VMSOCK_LIB(listen)
    cmp w0, #0
    b.lt Lo_cam_errno_dong_a
    ldr w0, [x20]
    b Lo_cam_ve_a
Lo_cam_chap_nhan_a:
// Dùng poll giới hạn accept kể cả khi nền tảng bỏ qua SO_RCVTIMEO.
    ldr w9, [x20]
    cmp w9, #0
    b.lt Lo_cam_sai_a
    str w9, [x20, #96]
    mov w9, #1 // POLLIN
    strh w9, [x20, #100]
    strh wzr, [x20, #102]
    ldr w9, [x20, #12]
    cmp w9, #1
    b.lt Lo_cam_sai_a
    bl Lo_cam_han_khoi_dong_a
    cmp w0, #0
    b.lt Lo_cam_ve_a
Lo_cam_chap_nhan_tham_do_lap_a:
    bl Lo_cam_han_con_lai_a
    cmp w0, #0
    b.lt Lo_cam_ve_a
    b.eq Lo_cam_chap_nhan_qua_han_a
    mov w2, w0
    add x0, x20, #96
    mov w1, #1
    bl VMSOCK_LIB(poll)
    cmp w0, #0
    b.lt Lo_cam_chap_nhan_tham_do_loi_a
    b.eq Lo_cam_chap_nhan_qua_han_a
    ldr w0, [x20]
    cmp w0, #0
    b.lt Lo_cam_sai_a
    mov x1, #0
    mov x2, #0
    bl VMSOCK_LIB(accept)
    cmp w0, #0
    b.lt Lo_cam_errno_ve_a
    str w0, [x20]
    bl Lo_cam_khong_sigpipe_a
    cmp w0, #0
    b.lt Lo_cam_dong_loi_a
    bl Lo_cam_dat_thoi_gian_cho_a
    cmp w0, #0
    b.lt Lo_cam_dong_loi_a
    ldr w0, [x20]
    b Lo_cam_ve_a
Lo_cam_chap_nhan_qua_han_a:
    mov w9, #VMSOCK_TIMEDOUT
    str w9, [x20, #28]
    neg w0, w9
    b Lo_cam_ve_a
Lo_cam_chap_nhan_tham_do_loi_a:
    bl Lo_cam_errno_a
    mov w9, #-4 // EINTR
    cmp w0, w9
    b.eq Lo_cam_chap_nhan_tham_do_lap_a
    b Lo_cam_ve_a
Lo_cam_nhap_xuat_a:
    ldr w9, [x20, #20]
    cmp w9, #1
    b.lt Lo_cam_sai_a
    mov w10, #65536
    cmp w9, w10
    b.gt Lo_cam_sai_a
    ldr w21, [x20, #16]
// Yêu cầu độ lệch + độ dài không vượt kích thước vùng nhớ.
    mov w10, #5120
    movk w10, #1040, lsl #16
    cmp w21, w10
    b.hs Lo_cam_sai_a
    add w11, w21, w9
    cmp w11, w21
    b.lo Lo_cam_sai_a
    cmp w11, w10
    b.hi Lo_cam_sai_a
    bl VMSOCK_SYM(vpp_native_memory_base)
    add x1, x0, w21, uxtw
    ldr w0, [x20]
    ldr w2, [x20, #20]
    mov w3, #VMSOCK_MSG
    cmp w19, #8
    b.eq Lo_cam_nhan_goi_a
    cmp w19, #9
    b.eq Lo_cam_gui_goi_a
    cmp w19, #3
    b.ne 1f
    bl VMSOCK_LIB(send)
    b 2f
1:  bl VMSOCK_LIB(recv)
2:  cmp w0, #0
    b.lt Lo_cam_errno_ve_a
    b Lo_cam_ve_a
Lo_cam_nhan_goi_a:
    mov w3, #0
    mov w9, #32
    str w9, [x20, #112]
    add x4, x20, #48
    add x5, x20, #112
    bl VMSOCK_LIB(recvfrom)
    cmp w0, #0
    b.lt Lo_cam_errno_ve_a
    b Lo_cam_ve_a
Lo_cam_gui_goi_a:
    ldr w5, [x20, #112]
    cmp w5, #16
    b.ne Lo_cam_sai_a
    add x4, x20, #48
    bl VMSOCK_LIB(sendto)
    cmp w0, #0
    b.lt Lo_cam_errno_ve_a
    b Lo_cam_ve_a
Lo_cam_dong_a:
    ldr w0, [x20]
    cmp w0, #0
    b.lt Lo_cam_sai_a
    bl VMSOCK_LIB(close)
    cmp w0, #0
    b.lt Lo_cam_errno_ve_a
    mov w9, #-1
    str w9, [x20]
    mov w0, #0
    b Lo_cam_ve_a
Lo_cam_thoi_gian_cho_a:
    ldr w9, [x20, #12]
    cmp w9, #1
    b.lt Lo_cam_sai_a
    mov w10, #0x93e0
    movk w10, #4, lsl #16
    cmp w9, w10
    b.gt Lo_cam_sai_a
    ldr w9, [x20]
    cmp w9, #0
    b.lt Lo_cam_sai_a
    bl Lo_cam_dat_thoi_gian_cho_a
    b Lo_cam_ve_a
Lo_cam_dat_thoi_gian_cho_a:
    str x30, [sp, #48]
    ldr w9, [x20, #12]
    mov w10, #1000
    udiv w11, w9, w10
    msub w12, w11, w10, w9
    mov w13, #1000
    mul w12, w12, w13
    stp x11, x12, [x20, #80]
    ldr w0, [x20]
    mov w1, #VMSOCK_LEVEL
    mov w2, #VMSOCK_RCVTIMEO
    add x3, x20, #80
    mov w4, #16
    bl VMSOCK_LIB(setsockopt)
    cmp w0, #0
    b.lt Lo_cam_thoi_gian_cho_errno_a
    ldr w0, [x20]
    mov w1, #VMSOCK_LEVEL
    mov w2, #VMSOCK_SNDTIMEO
    add x3, x20, #80
    mov w4, #16
    bl VMSOCK_LIB(setsockopt)
    cmp w0, #0
    b.lt Lo_cam_thoi_gian_cho_errno_a
    mov w0, #0
    b Lo_cam_thoi_gian_cho_tra_ve_a
Lo_cam_thoi_gian_cho_errno_a:
    bl Lo_cam_errno_a
Lo_cam_thoi_gian_cho_tra_ve_a:
    ldr x30, [sp, #48]
    ret
Lo_cam_khong_sigpipe_a:
    str x30, [sp, #48]
#ifdef __APPLE__
    mov w9, #1
    str w9, [x20, #96]
    ldr w0, [x20]
    mov w1, #VMSOCK_LEVEL
    mov w2, #VMSOCK_NOSIGPIPE
    add x3, x20, #96
    mov w4, #4
    bl VMSOCK_LIB(setsockopt)
    cmp w0, #0
    b.lt Lo_cam_khong_sigpipe_errno_a
#endif
    mov w0, #0
    b Lo_cam_khong_sigpipe_tra_ve_a
Lo_cam_khong_sigpipe_errno_a:
    bl Lo_cam_errno_a
Lo_cam_khong_sigpipe_tra_ve_a:
    ldr x30, [sp, #48]
    ret
// Thời hạn đơn điệu tuyệt đối tránh kéo dài thời gian chờ sau EINTR. Khung gọi: timespec@64, thời
// hạn mili giây@80, thanh ghi trở về@88.
Lo_cam_han_khoi_dong_a:
    str x30, [sp, #88]
    mov w0, #VMSOCK_CLOCK
    add x1, sp, #64
    bl VMSOCK_LIB(clock_gettime)
    cmp w0, #0
    b.lt Lo_cam_han_errno_a
    bl Lo_cam_hien_tai_mili_giay_a
    ldr w9, [x20, #12]
    add x0, x0, w9, uxtw
    str x0, [sp, #80]
    mov w0, #0
    b Lo_cam_han_ve_a
Lo_cam_han_con_lai_a:
    str x30, [sp, #88]
    mov w0, #VMSOCK_CLOCK
    add x1, sp, #64
    bl VMSOCK_LIB(clock_gettime)
    cmp w0, #0
    b.lt Lo_cam_han_errno_a
    bl Lo_cam_hien_tai_mili_giay_a
    ldr x9, [sp, #80]
    subs x0, x9, x0
    b.gt Lo_cam_han_ve_a
    mov w0, #0
    b Lo_cam_han_ve_a
Lo_cam_han_errno_a:
    bl Lo_cam_errno_a
Lo_cam_han_ve_a:
    ldr x30, [sp, #88]
    ret
Lo_cam_hien_tai_mili_giay_a:
    ldp x9, x10, [sp, #64]
    mov x11, #1000
    mul x9, x9, x11
    movz x11, #0x4240
    movk x11, #0xf, lsl #16
    udiv x10, x10, x11
    add x0, x9, x10
    ret
Lo_cam_errno_dong_a:
    bl Lo_cam_errno_a
Lo_cam_dong_loi_a:
    mov w23, w0
    ldr w0, [x20]
    bl VMSOCK_LIB(close)
    mov w9, #-1
    str w9, [x20]
    mov w0, w23
    b Lo_cam_ve_a
Lo_cam_sai_mo_ta_a:
    mov w0, #-22
    b Lo_cam_dong_loi_a
Lo_cam_errno_ve_a:
    bl Lo_cam_errno_a
    b Lo_cam_ve_a
Lo_cam_errno_a:
    str x30, [sp, #56]
#ifdef __APPLE__
    bl VMSOCK_LIB(__error)
#else
    bl VMSOCK_LIB(__errno_location)
#endif
    ldr w9, [x0]
    str w9, [x20, #28]
    neg w0, w9
    ldr x30, [sp, #56]
    ret
Lo_cam_sai_a:
    mov w0, #-22
Lo_cam_ve_a:
    ldp x23, x30, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #96
    ret
#else
#if defined(__linux__) && defined(__x86_64__)
// Linux dùng chung mã ổ cắm gọi syscall trực tiếp với bộ thực thi độc lập. macOS x86-64 dùng mã
// POSIX bên dưới.
#include "../../linux/vpp_linux_socket_x86_64.S"
#else
#include "vpp_vm_socket_x86_64.inc"
#endif
#endif
