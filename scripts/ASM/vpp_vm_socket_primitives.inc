// VM socket boundary: op 0 connect, 1 listen, 2 accept, 3 send,
// 4 recv, 5 close, 6 set timeout; 7 UDP bind, 8 recvfrom, 9 sendto,
// 10 local port. Descriptor ownership and DNS mock policy are in V++.
// control (128 bytes): fd@0, kind@4, port@8, timeout@12,
// data offset@16, length@20, IP length@24, errno@28, IP@32,
// sockaddr@48, timeval@80; pollfd@96; saved flags@104; error@108; socklen@112.
// Monotonic poll deadline and timespec are private ASM stack locals.
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
    cbz x0, Lsock_bad_a
    cmp w19, #10
    b.hi Lsock_bad_a
    // arena is 68162560; each control is 128 bytes, 16-aligned.
    mov w9, #4992
    movk w9, #1040, lsl #16
    cmp w21, w9
    b.hi Lsock_bad_a
    tst w21, #15
    b.ne Lsock_bad_a
    add x20, x0, w21, uxtw
    str wzr, [x20, #28]
    cmp w19, #7
    b.eq Lsock_udp_bind_a
    cmp w19, #8
    b.eq Lsock_io_a
    cmp w19, #9
    b.eq Lsock_io_a
    cmp w19, #10
    b.eq Lsock_port_a
    cbz w19, Lsock_connect_a
    cmp w19, #1
    b.eq Lsock_listen_a
    cmp w19, #2
    b.eq Lsock_accept_a
    cmp w19, #3
    b.eq Lsock_io_a
    cmp w19, #4
    b.eq Lsock_io_a
    cmp w19, #5
    b.eq Lsock_close_a
    b Lsock_timeout_a
Lsock_addr_a:
    // w0 = AF_INET/AF_INET6. Build sockaddr in reserved control area.
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
Lsock_udp_bind_a:
    ldr w9, [x20, #8]
    cmp w9, #0
    b.lt Lsock_bad_a
    mov w10, #65535
    cmp w9, w10
    b.gt Lsock_bad_a
    ldr w9, [x20, #12]
    cmp w9, #1
    b.lt Lsock_bad_a
    mov w10, #0x93e0
    movk w10, #4, lsl #16
    cmp w9, w10
    b.gt Lsock_bad_a
    mov w0, #2
    bl Lsock_addr_a
    mov w0, #2
    mov w1, #2
    mov w2, #0
    bl VMSOCK_LIB(socket)
    cmp w0, #0
    b.lt Lsock_errno_ret_a
    str w0, [x20]
    bl Lsock_set_timeout_a
    cmp w0, #0
    b.lt Lsock_close_error_a
    ldr w0, [x20]
    add x1, x20, #48
    mov w2, #16
    bl VMSOCK_LIB(bind)
    cmp w0, #0
    b.lt Lsock_errno_close_a
    ldr w0, [x20]
    b Lsock_ret_a
Lsock_port_a:
    ldr w0, [x20]
    cmp w0, #0
    b.lt Lsock_bad_a
    mov w9, #32
    str w9, [x20, #112]
    add x1, x20, #48
    add x2, x20, #112
    bl VMSOCK_LIB(getsockname)
    cmp w0, #0
    b.lt Lsock_errno_ret_a
    ldrh w0, [x20, #50]
    rev16 w0, w0
    str w0, [x20, #8]
    b Lsock_ret_a
Lsock_connect_a:
    ldr w9, [x20, #24]
    cmp w9, #4
    b.eq 1f
    cmp w9, #16
    b.ne Lsock_bad_a
1:  ldr w9, [x20, #8]
    cmp w9, #1
    b.lt Lsock_bad_a
    mov w10, #65535
    cmp w9, w10
    b.gt Lsock_bad_a
    ldr w9, [x20, #4]
    cmp w9, #1
    b.hi Lsock_bad_a
    mov w0, #2
    ldr w9, [x20, #24]
    cmp w9, #16
    b.ne 2f
    mov w0, #VMSOCK_INET6
2:  bl Lsock_addr_a
    mov w0, w22
    mov w1, #1
    ldr w9, [x20, #4]
    cbz w9, 3f
    mov w1, #2
3:  mov w2, #0
    bl VMSOCK_LIB(socket)
    cmp w0, #0
    b.lt Lsock_errno_ret_a
    str w0, [x20]
    bl Lsock_nosigpipe_a
    cmp w0, #0
    b.lt Lsock_close_error_a
    ldr w9, [x20, #12]
    cmp w9, #1
    b.lt Lsock_bad_fd_a
    mov w10, #0x93e0
    movk w10, #4, lsl #16
    cmp w9, w10
    b.gt Lsock_bad_fd_a
    bl Lsock_set_timeout_a
    cmp w0, #0
    b.lt Lsock_close_error_a
    // SO_SNDTIMEO does not bound a blocking connect. Temporarily use
    // O_NONBLOCK + poll, then restore the original descriptor flags.
    ldr w0, [x20]
    mov w1, #3 // F_GETFL
    bl VMSOCK_LIB(fcntl)
    cmp w0, #0
    b.lt Lsock_errno_close_a
    str w0, [x20, #104]
    orr w2, w0, #VMSOCK_NONBLOCK
    ldr w0, [x20]
    mov w1, #4 // F_SETFL
    bl VMSOCK_LIB(fcntl)
    cmp w0, #0
    b.lt Lsock_errno_close_a
    ldr w0, [x20]
    add x1, x20, #48
    mov w2, #16
    ldr w9, [x20, #24]
    cmp w9, #16
    b.ne 4f
    mov w2, #28
4:  bl VMSOCK_LIB(connect)
    cmp w0, #0
    b.ge Lsock_connected_a
    bl Lsock_errno_a
    mov w9, #-VMSOCK_INPROGRESS
    cmp w0, w9
    b.eq Lsock_connect_pending_a
    mov w9, #-4 // EINTR from nonblocking connect is also pending.
    cmp w0, w9
    b.ne Lsock_connect_restore_error_a
Lsock_connect_pending_a:
    ldr w9, [x20]
    str w9, [x20, #96]
    mov w9, #4 // POLLOUT
    strh w9, [x20, #100]
    strh wzr, [x20, #102]
    bl Lsock_deadline_start_a
    cmp w0, #0
    b.lt Lsock_connect_restore_error_a
Lsock_connect_poll_loop_a:
    bl Lsock_deadline_remaining_a
    cmp w0, #0
    b.lt Lsock_connect_restore_error_a
    b.eq Lsock_connect_timedout_a
    mov w2, w0
    add x0, x20, #96
    mov w1, #1
    bl VMSOCK_LIB(poll)
    cmp w0, #0
    b.lt Lsock_connect_poll_error_a
    b.eq Lsock_connect_timedout_a
    ldr w0, [x20]
    mov w1, #VMSOCK_LEVEL
    mov w2, #VMSOCK_SOERROR
    add x3, x20, #108
    mov w9, #4
    str w9, [x20, #112]
    add x4, x20, #112
    bl VMSOCK_LIB(getsockopt)
    cmp w0, #0
    b.lt Lsock_connect_poll_error_a
    ldr w9, [x20, #108]
    cbz w9, Lsock_connected_a
    str w9, [x20, #28]
    neg w0, w9
    b Lsock_connect_restore_error_a
Lsock_connect_timedout_a:
    mov w9, #VMSOCK_TIMEDOUT
    str w9, [x20, #28]
    neg w0, w9
    b Lsock_connect_restore_error_a
Lsock_connect_poll_error_a:
    bl Lsock_errno_a
    mov w9, #-4 // EINTR
    cmp w0, w9
    b.eq Lsock_connect_poll_loop_a
Lsock_connect_restore_error_a:
    mov w23, w0
    ldr w0, [x20]
    mov w1, #4
    ldr w2, [x20, #104]
    bl VMSOCK_LIB(fcntl)
    mov w0, w23
    b Lsock_close_error_a
Lsock_connected_a:
    ldr w0, [x20]
    mov w1, #4
    ldr w2, [x20, #104]
    bl VMSOCK_LIB(fcntl)
    cmp w0, #0
    b.lt Lsock_errno_close_a
    ldr w0, [x20]
    b Lsock_ret_a
Lsock_listen_a:
    ldr w9, [x20, #8]
    cmp w9, #1
    b.lt Lsock_bad_a
    mov w10, #65535
    cmp w9, w10
    b.gt Lsock_bad_a
    ldr w9, [x20, #20]
    cmp w9, #1
    b.lt Lsock_bad_a
    cmp w9, #1024
    b.gt Lsock_bad_a
    mov w0, #2
    bl Lsock_addr_a
    mov w0, #2
    mov w1, #1
    mov w2, #0
    bl VMSOCK_LIB(socket)
    cmp w0, #0
    b.lt Lsock_errno_ret_a
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
    b.lt Lsock_errno_close_a
    ldr w0, [x20]
    add x1, x20, #48
    mov w2, #16
    bl VMSOCK_LIB(bind)
    cmp w0, #0
    b.lt Lsock_errno_close_a
    ldr w0, [x20]
    ldr w1, [x20, #20]
    bl VMSOCK_LIB(listen)
    cmp w0, #0
    b.lt Lsock_errno_close_a
    ldr w0, [x20]
    b Lsock_ret_a
Lsock_accept_a:
    // poll bounds accept even on platforms where SO_RCVTIMEO is ignored.
    ldr w9, [x20]
    cmp w9, #0
    b.lt Lsock_bad_a
    str w9, [x20, #96]
    mov w9, #1 // POLLIN
    strh w9, [x20, #100]
    strh wzr, [x20, #102]
    ldr w9, [x20, #12]
    cmp w9, #1
    b.lt Lsock_bad_a
    bl Lsock_deadline_start_a
    cmp w0, #0
    b.lt Lsock_ret_a
Lsock_accept_poll_loop_a:
    bl Lsock_deadline_remaining_a
    cmp w0, #0
    b.lt Lsock_ret_a
    b.eq Lsock_accept_timedout_a
    mov w2, w0
    add x0, x20, #96
    mov w1, #1
    bl VMSOCK_LIB(poll)
    cmp w0, #0
    b.lt Lsock_accept_poll_error_a
    b.eq Lsock_accept_timedout_a
    ldr w0, [x20]
    cmp w0, #0
    b.lt Lsock_bad_a
    mov x1, #0
    mov x2, #0
    bl VMSOCK_LIB(accept)
    cmp w0, #0
    b.lt Lsock_errno_ret_a
    str w0, [x20]
    bl Lsock_nosigpipe_a
    cmp w0, #0
    b.lt Lsock_close_error_a
    bl Lsock_set_timeout_a
    cmp w0, #0
    b.lt Lsock_close_error_a
    ldr w0, [x20]
    b Lsock_ret_a
Lsock_accept_timedout_a:
    mov w9, #VMSOCK_TIMEDOUT
    str w9, [x20, #28]
    neg w0, w9
    b Lsock_ret_a
Lsock_accept_poll_error_a:
    bl Lsock_errno_a
    mov w9, #-4 // EINTR
    cmp w0, w9
    b.eq Lsock_accept_poll_loop_a
    b Lsock_ret_a
Lsock_io_a:
    ldr w9, [x20, #20]
    cmp w9, #1
    b.lt Lsock_bad_a
    mov w10, #65536
    cmp w9, w10
    b.gt Lsock_bad_a
    ldr w21, [x20, #16]
    // Require offset + length <= total arena size.
    mov w10, #5120
    movk w10, #1040, lsl #16
    cmp w21, w10
    b.hs Lsock_bad_a
    add w11, w21, w9
    cmp w11, w21
    b.lo Lsock_bad_a
    cmp w11, w10
    b.hi Lsock_bad_a
    bl VMSOCK_SYM(vpp_native_memory_base)
    add x1, x0, w21, uxtw
    ldr w0, [x20]
    ldr w2, [x20, #20]
    mov w3, #VMSOCK_MSG
    cmp w19, #8
    b.eq Lsock_recvfrom_a
    cmp w19, #9
    b.eq Lsock_sendto_a
    cmp w19, #3
    b.ne 1f
    bl VMSOCK_LIB(send)
    b 2f
1:  bl VMSOCK_LIB(recv)
2:  cmp w0, #0
    b.lt Lsock_errno_ret_a
    b Lsock_ret_a
Lsock_recvfrom_a:
    mov w3, #0
    mov w9, #32
    str w9, [x20, #112]
    add x4, x20, #48
    add x5, x20, #112
    bl VMSOCK_LIB(recvfrom)
    cmp w0, #0
    b.lt Lsock_errno_ret_a
    b Lsock_ret_a
Lsock_sendto_a:
    ldr w5, [x20, #112]
    cmp w5, #16
    b.ne Lsock_bad_a
    add x4, x20, #48
    bl VMSOCK_LIB(sendto)
    cmp w0, #0
    b.lt Lsock_errno_ret_a
    b Lsock_ret_a
Lsock_close_a:
    ldr w0, [x20]
    cmp w0, #0
    b.lt Lsock_bad_a
    bl VMSOCK_LIB(close)
    cmp w0, #0
    b.lt Lsock_errno_ret_a
    mov w9, #-1
    str w9, [x20]
    mov w0, #0
    b Lsock_ret_a
Lsock_timeout_a:
    ldr w9, [x20, #12]
    cmp w9, #1
    b.lt Lsock_bad_a
    mov w10, #0x93e0
    movk w10, #4, lsl #16
    cmp w9, w10
    b.gt Lsock_bad_a
    ldr w9, [x20]
    cmp w9, #0
    b.lt Lsock_bad_a
    bl Lsock_set_timeout_a
    b Lsock_ret_a
Lsock_set_timeout_a:
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
    b.lt Lsock_timeout_errno_a
    ldr w0, [x20]
    mov w1, #VMSOCK_LEVEL
    mov w2, #VMSOCK_SNDTIMEO
    add x3, x20, #80
    mov w4, #16
    bl VMSOCK_LIB(setsockopt)
    cmp w0, #0
    b.lt Lsock_timeout_errno_a
    mov w0, #0
    b Lsock_timeout_return_a
Lsock_timeout_errno_a:
    bl Lsock_errno_a
Lsock_timeout_return_a:
    ldr x30, [sp, #48]
    ret
Lsock_nosigpipe_a:
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
    b.lt Lsock_nosigpipe_errno_a
#endif
    mov w0, #0
    b Lsock_nosigpipe_return_a
Lsock_nosigpipe_errno_a:
    bl Lsock_errno_a
Lsock_nosigpipe_return_a:
    ldr x30, [sp, #48]
    ret
// Absolute monotonic deadline avoids extending timeout after EINTR.
// frame: timespec @64, deadline milliseconds @80, helper LR @88.
Lsock_deadline_start_a:
    str x30, [sp, #88]
    mov w0, #VMSOCK_CLOCK
    add x1, sp, #64
    bl VMSOCK_LIB(clock_gettime)
    cmp w0, #0
    b.lt Lsock_deadline_errno_a
    bl Lsock_now_ms_a
    ldr w9, [x20, #12]
    add x0, x0, w9, uxtw
    str x0, [sp, #80]
    mov w0, #0
    b Lsock_deadline_ret_a
Lsock_deadline_remaining_a:
    str x30, [sp, #88]
    mov w0, #VMSOCK_CLOCK
    add x1, sp, #64
    bl VMSOCK_LIB(clock_gettime)
    cmp w0, #0
    b.lt Lsock_deadline_errno_a
    bl Lsock_now_ms_a
    ldr x9, [sp, #80]
    subs x0, x9, x0
    b.gt Lsock_deadline_ret_a
    mov w0, #0
    b Lsock_deadline_ret_a
Lsock_deadline_errno_a:
    bl Lsock_errno_a
Lsock_deadline_ret_a:
    ldr x30, [sp, #88]
    ret
Lsock_now_ms_a:
    ldp x9, x10, [sp, #64]
    mov x11, #1000
    mul x9, x9, x11
    movz x11, #0x4240
    movk x11, #0xf, lsl #16
    udiv x10, x10, x11
    add x0, x9, x10
    ret
Lsock_errno_close_a:
    bl Lsock_errno_a
Lsock_close_error_a:
    mov w23, w0
    ldr w0, [x20]
    bl VMSOCK_LIB(close)
    mov w9, #-1
    str w9, [x20]
    mov w0, w23
    b Lsock_ret_a
Lsock_bad_fd_a:
    mov w0, #-22
    b Lsock_close_error_a
Lsock_errno_ret_a:
    bl Lsock_errno_a
    b Lsock_ret_a
Lsock_errno_a:
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
Lsock_bad_a:
    mov w0, #-22
Lsock_ret_a:
    ldp x23, x30, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #96
    ret
#else
#include "vpp_vm_socket_x86_64.inc"
#endif
