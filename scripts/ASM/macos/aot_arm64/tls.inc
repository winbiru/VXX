// Verified TLS client for the macOS ARM64 standalone VM and native AOT.
// Machine socket modes 11 upgrade, 12 write, 13 read, 14 dispose.
// Control block (160 bytes) stores SSLContextRef@120, framework handles
// @128/136, verified@144 and CFRelease@152. All pointers stay private to the OS boundary.
// Require a successful handshake and a hostname-bound SecTrust evaluation.

.text
.p2align 2
.globl _vpp_native_tls_socket
_vpp_native_tls_socket:
    // Space for CF result pointers and two monotonic timespec values.
    stp x19, x20, [sp, #-144]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    mov w19, w0
    mov x20, x1
    mov w27, #0
    cmp w19, #14
    b.eq L_tls_close
    ldr w9, [x20]
    cmp w9, #0
    b.lt L_tls_invalid
    cmp w19, #11
    b.eq L_tls_upgrade
    ldr x22, [x20, #120]
    cbz x22, L_tls_invalid
    ldr x21, [x20, #128]
    cbz x21, L_tls_invalid
    ldr w9, [x20, #144]
    cmp w9, #1
    b.ne L_tls_invalid
    ldr w24, [x20, #20]
    cmp w24, #1
    b.lt L_tls_invalid
    mov w9, #65536
    cmp w24, w9
    b.gt L_tls_invalid
    ldr w23, [x20, #16]
    mov w9, #5120
    movk w9, #2064, lsl #16 // 135271424 bytes total arena
    cmp w23, w9
    b.hs L_tls_invalid
    add w10, w23, w24
    cmp w10, w23
    b.lo L_tls_invalid
    cmp w10, w9
    b.hi L_tls_invalid
    bl _vpp_native_memory_base
    cbz x0, L_tls_invalid
    add x23, x0, w23, uxtw
    cmp w19, #12
    b.eq L_tls_write
    cmp w19, #13
    b.eq L_tls_read
    b L_tls_invalid

L_tls_upgrade:
    ldr x9, [x20, #120]
    cbnz x9, L_tls_invalid
    mov x25, #0 // SecPolicyRef
    mov x28, #0 // SecTrustRef
    str wzr, [x20, #144]
    // The socket's connect-time scratch area is free after connect. During
    // handshake it holds a pointer to our absolute monotonic deadline.
    str xzr, [x20, #104]
    ldr w24, [x20, #20] // hostname length
    cmp w24, #1
    b.lt L_tls_invalid
    cmp w24, #253
    b.gt L_tls_invalid
    ldr w23, [x20, #16]
    mov w9, #5120
    movk w9, #2064, lsl #16
    cmp w23, w9
    b.hs L_tls_invalid
    add w10, w23, w24
    cmp w10, w23
    b.lo L_tls_invalid
    cmp w10, w9
    b.hi L_tls_invalid
    bl _vpp_native_memory_base
    cbz x0, L_tls_invalid
    add x23, x0, w23, uxtw
    // Open both frameworks before creating an SSL context, so every
    // successful create has a functioning CFRelease cleanup path.
    adrp x0, L_tls_security_path@PAGE
    add x0, x0, L_tls_security_path@PAGEOFF
    mov w1, #2 // RTLD_NOW
    bl _dlopen
    cbz x0, L_tls_invalid
    mov x21, x0
    str x21, [x20, #128]
    adrp x0, L_tls_core_path@PAGE
    add x0, x0, L_tls_core_path@PAGEOFF
    mov w1, #2
    bl _dlopen
    cbz x0, L_tls_fail
    str x0, [x20, #136]
    // Resolve release before creating any retained CF/Security object.
    adrp x1, L_tls_release_name@PAGE
    add x1, x1, L_tls_release_name@PAGEOFF
    bl _dlsym
    cbz x0, L_tls_fail
    str x0, [x20, #152]
    mov x0, x21
    adrp x1, L_tls_create_name@PAGE
    add x1, x1, L_tls_create_name@PAGEOFF
    bl _dlsym
    cbz x0, L_tls_fail
    mov x9, x0
    mov x0, #0 // CFAllocatorRef
    mov w1, #1 // kSSLClientSide
    mov w2, #0 // kSSLStreamType
    blr x9
    cbz x0, L_tls_fail
    mov x22, x0
    str x22, [x20, #120]
    mov x0, x21
    adrp x1, L_tls_io_name@PAGE
    add x1, x1, L_tls_io_name@PAGEOFF
    bl _dlsym
    cbz x0, L_tls_fail
    mov x9, x0
    mov x0, x22
    adrp x1, L_tls_callback_read@PAGE
    add x1, x1, L_tls_callback_read@PAGEOFF
    adrp x2, L_tls_callback_write@PAGE
    add x2, x2, L_tls_callback_write@PAGEOFF
    blr x9
    cbnz w0, L_tls_fail
    mov x0, x21
    adrp x1, L_tls_connection_name@PAGE
    add x1, x1, L_tls_connection_name@PAGEOFF
    bl _dlsym
    cbz x0, L_tls_fail
    mov x9, x0
    mov x0, x22
    mov x1, x20 // Stable control-block pointer for read/write callbacks.
    blr x9
    cbnz w0, L_tls_fail
    mov x0, x21
    adrp x1, L_tls_domain_name@PAGE
    add x1, x1, L_tls_domain_name@PAGEOFF
    bl _dlsym
    cbz x0, L_tls_fail
    mov x9, x0
    mov x0, x22
    mov x1, x23
    mov x2, x24
    blr x9
    cbnz w0, L_tls_fail
    // Pause at server authentication so peer trust is evaluated explicitly
    // before completing the handshake. The peer domain also supplies SNI.
    mov x0, x21
    adrp x1, L_tls_break_name@PAGE
    add x1, x1, L_tls_break_name@PAGEOFF
    bl _dlsym
    cbz x0, L_tls_fail
    mov x9, x0
    mov x0, x22
    mov w1, #0 // kSSLSessionOptionBreakOnServerAuth
    mov w2, #1 // true
    blr x9
    cbnz w0, L_tls_fail
    mov x0, x21
    adrp x1, L_tls_min_name@PAGE
    add x1, x1, L_tls_min_name@PAGEOFF
    bl _dlsym
    cbz x0, L_tls_fail
    mov x9, x0
    mov x0, x22
    mov w1, #8 // kTLSProtocol12, reject TLS 1.1 and older.
    blr x9
    cbnz w0, L_tls_fail
    // Enforce one deadline for the complete handshake. Socket read timeouts
    // alone restart on each retry, allowing peers to extend the handshake.
    ldr w9, [x20, #12]
    cmp w9, #1
    b.lt L_tls_fail
    mov w10, #0x93e0
    movk w10, #4, lsl #16 // 300000 milliseconds
    cmp w9, w10
    b.gt L_tls_fail
    mov w0, #6 // CLOCK_MONOTONIC on Darwin
    add x1, sp, #112
    bl _clock_gettime
    cbnz w0, L_tls_fail
    ldr w9, [x20, #12]
    ldp x11, x12, [sp, #112]
    mov w13, #1000
    udiv w14, w9, w13
    msub w15, w14, w13, w9
    add x11, x11, x14
    mov w16, #0x4240
    movk w16, #0xf, lsl #16 // 1000000 nanoseconds/millisecond
    madd x12, x15, x16, x12
    mov w16, #0xca00
    movk w16, #0x3b9a, lsl #16 // 1000000000 nanoseconds/second
    cmp x12, x16
    b.lt L_tls_deadline_ready
    add x11, x11, #1
    sub x12, x12, x16
L_tls_deadline_ready:
    stp x11, x12, [sp, #112]
    add x9, sp, #112
    str x9, [x20, #104]
L_tls_handshake:
    mov w0, #6 // CLOCK_MONOTONIC
    add x1, sp, #128
    bl _clock_gettime
    cbnz w0, L_tls_fail
    ldp x9, x10, [sp, #128]
    ldr x11, [sp, #112]
    cmp x9, x11
    b.gt L_tls_fail
    b.lt L_tls_handshake_attempt
    ldr x11, [sp, #120]
    cmp x10, x11
    b.hs L_tls_fail
L_tls_handshake_attempt:
    mov x0, x21
    adrp x1, L_tls_handshake_name@PAGE
    add x1, x1, L_tls_handshake_name@PAGEOFF
    bl _dlsym
    cbz x0, L_tls_fail
    mov x9, x0
    mov x0, x22
    blr x9
    cbz w0, L_tls_handshake_complete
    mov w9, #9841 // errSSLPeerAuthCompleted
    neg w9, w9
    cmp w0, w9
    b.eq L_tls_verify_trust
    mov w9, #9803 // errSSLWouldBlock for partial I/O.
    neg w9, w9
    cmp w0, w9
    b.ne L_tls_fail
    b L_tls_handshake

L_tls_handshake_complete:
    // A successful handshake without the explicit trust callback is invalid.
    ldr w9, [x20, #144]
    cmp w9, #1
    b.ne L_tls_fail
    str xzr, [x20, #104]
    b L_tls_success

L_tls_verify_trust:
    ldr w9, [x20, #144]
    cbnz w9, L_tls_fail // Never accept a second authentication break.
    // Verify the peer against the requested hostname and the system's
    // trust anchors before the socket can be marked usable by the VM.
    mov x26, #0 // CFStringRef, handshake retry count no longer needed.
    mov x0, x21
    adrp x1, L_tls_peer_trust_name@PAGE
    add x1, x1, L_tls_peer_trust_name@PAGEOFF
    bl _dlsym
    cbz x0, L_tls_fail
    mov x9, x0
    mov x0, x22
    add x1, sp, #104
    str xzr, [sp, #104]
    blr x9
    cbnz w0, L_tls_fail
    ldr x28, [sp, #104]
    cbz x28, L_tls_fail

    ldr x0, [x20, #136]
    adrp x1, L_tls_string_name@PAGE
    add x1, x1, L_tls_string_name@PAGEOFF
    bl _dlsym
    cbz x0, L_tls_trust_failed
    mov x9, x0
    mov x0, #0 // Default CFAllocatorRef
    mov x1, x23 // Hostname is restricted to ASCII/UTF-8.
    mov x2, x24
    mov w3, #0x100
    movk w3, #0x800, lsl #16 // kCFStringEncodingUTF8
    mov w4, #0
    blr x9
    mov x26, x0
    cbz x26, L_tls_trust_failed

    mov x0, x21
    adrp x1, L_tls_policy_name@PAGE
    add x1, x1, L_tls_policy_name@PAGEOFF
    bl _dlsym
    cbz x0, L_tls_trust_failed
    mov x9, x0
    mov w0, #1 // Server certificate
    mov x1, x26
    blr x9
    mov x25, x0
    cbz x25, L_tls_trust_failed

    mov x0, x21
    adrp x1, L_tls_set_policy_name@PAGE
    add x1, x1, L_tls_set_policy_name@PAGEOFF
    bl _dlsym
    cbz x0, L_tls_trust_failed
    mov x9, x0
    mov x0, x28
    mov x1, x25
    blr x9
    cbnz w0, L_tls_trust_failed

    mov x0, x21
    adrp x1, L_tls_evaluate_name@PAGE
    add x1, x1, L_tls_evaluate_name@PAGEOFF
    bl _dlsym
    cbz x0, L_tls_trust_failed
    mov x9, x0
    mov x0, x28
    mov x1, #0 // No CFErrorRef is requested.
    blr x9
    cbz w0, L_tls_trust_failed
    mov w27, #0
    b L_tls_trust_cleanup
L_tls_trust_failed:
    mov w27, #1
L_tls_trust_cleanup:
    ldr x24, [x20, #152] // Resolved before allocating any CF objects.
    cbz x25, L_tls_release_hostname
    mov x0, x25
    blr x24
L_tls_release_hostname:
    cbz x26, L_tls_release_trust
    mov x0, x26
    blr x24
L_tls_release_trust:
    mov x0, x28
    blr x24
    cbnz w27, L_tls_fail
    mov w9, #1
    str w9, [x20, #144]
    // Keep the original deadline for the remainder of the handshake.
    b L_tls_handshake

L_tls_write:
    adrp x1, L_tls_write_name@PAGE
    add x1, x1, L_tls_write_name@PAGEOFF
    b L_tls_io
L_tls_read:
    adrp x1, L_tls_read_name@PAGE
    add x1, x1, L_tls_read_name@PAGEOFF
L_tls_io:
    mov x0, x21
    bl _dlsym
    cbz x0, L_tls_invalid
    mov x9, x0
    mov x0, x22
    mov x1, x23
    mov x2, x24
    add x3, sp, #96
    str xzr, [sp, #96]
    blr x9
    mov w25, w0
    ldr x24, [sp, #96]
    mov w9, #9805 // errSSLClosedGraceful
    neg w9, w9
    cmp w19, #13
    b.ne L_tls_io_check
    cmp w25, w9
    b.eq L_tls_eof
L_tls_io_check:
    cbz w25, L_tls_io_done
    mov w9, #9803 // errSSLWouldBlock with partial progress.
    neg w9, w9
    cmp w25, w9
    b.ne L_tls_invalid
    cbz x24, L_tls_invalid
L_tls_io_done:
    mov w0, w24
    b L_tls_return
L_tls_eof:
    // SecureTransport may report a graceful close along with final plaintext
    // bytes. Deliver those bytes before signaling EOF on the next read.
    mov w0, w24
    b L_tls_return

L_tls_close:
    // Never retain a pointer into a completed native call's stack frame.
    str xzr, [x20, #104]
    ldr x22, [x20, #120]
    cbz x22, L_tls_close_handles
    ldr x21, [x20, #128]
    cbz x21, L_tls_close_handles
    ldr w9, [x20, #144]
    cbz w9, L_tls_release
    mov x0, x21
    adrp x1, L_tls_shutdown_name@PAGE
    add x1, x1, L_tls_shutdown_name@PAGEOFF
    bl _dlsym
    cbz x0, L_tls_release
    mov x9, x0
    mov x0, x22
    blr x9
L_tls_release:
    ldr x9, [x20, #152]
    cbz x9, L_tls_close_handles
    mov x0, x22
    blr x9
    str xzr, [x20, #120]
L_tls_close_handles:
    str wzr, [x20, #144]
    str xzr, [x20, #152]
    ldr x0, [x20, #136]
    cbz x0, L_tls_close_security
    bl _dlclose
    str xzr, [x20, #136]
L_tls_close_security:
    ldr x0, [x20, #128]
    cbz x0, L_tls_close_done
    bl _dlclose
    str xzr, [x20, #128]
L_tls_close_done:
    cbnz w27, L_tls_invalid
    b L_tls_success
L_tls_fail:
    // Dispose of partially constructed sessions before returning failure.
    mov w27, #1
    b L_tls_close
L_tls_success:
    mov w0, #0
    b L_tls_return
L_tls_invalid:
    mov w0, #-1
L_tls_return:
    ldp x29, x30, [sp, #80]
    ldp x27, x28, [sp, #64]
    ldp x25, x26, [sp, #48]
    ldp x23, x24, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #144
    ret

// SecureTransport I/O callbacks. Handshake callbacks poll against the absolute
// deadline before each read/write so a peer cannot extend the timeout by
// sending fragments. Established streams use the socket's configured timeout.
// Return errSSLWouldBlock for short reads/writes, keeping the byte count.
L_tls_callback_read:
    stp x19, x20, [sp, #-80]!
    stp x21, x22, [sp, #16]
    stp x23, x30, [sp, #32]
    mov w22, #1 // POLLIN
    b L_tls_cb_start
L_tls_callback_write:
    stp x19, x20, [sp, #-80]!
    stp x21, x22, [sp, #16]
    stp x23, x30, [sp, #32]
    mov w22, #4 // POLLOUT
L_tls_cb_start:
    mov x19, x2
    mov x20, x0
    mov x21, x1
    ldr x23, [x20, #104]
    cbz x23, L_tls_cb_io
L_tls_cb_poll:
    mov w0, #6 // CLOCK_MONOTONIC on Darwin
    add x1, sp, #48
    bl _clock_gettime
    cbnz w0, L_tls_cb_abort
    ldp x9, x10, [x23] // deadline in seconds and nanoseconds
    ldp x11, x12, [sp, #48]
    subs x9, x9, x11
    b.lt L_tls_cb_abort
    subs x10, x10, x12
    b.ge L_tls_cb_duration
    sub x9, x9, #1
    mov w13, #0xca00
    movk w13, #0x3b9a, lsl #16 // 1 billion nanoseconds
    add x10, x10, x13
L_tls_cb_duration:
    cmp x9, #0
    b.lt L_tls_cb_abort
    // Round up to the next millisecond, never return 0 for time remaining.
    mov w13, #1000
    mul x9, x9, x13
    mov w13, #0x4240
    movk w13, #0xf, lsl #16 // 1 million nanoseconds
    sub x14, x13, #1
    add x10, x10, x14
    udiv x10, x10, x13
    add x9, x9, x10
    cbz x9, L_tls_cb_abort
    ldr w10, [x20]
    str w10, [sp, #64] // struct pollfd { fd, events, revents }
    strh w22, [sp, #68]
    strh wzr, [sp, #70]
    add x0, sp, #64
    mov x1, #1
    mov w2, w9
    bl _poll
    cbz w0, L_tls_cb_abort
    cmp w0, #0
    b.ge L_tls_cb_io
    bl ___error
    ldr w9, [x0]
    cmp w9, #4 // EINTR
    b.eq L_tls_cb_poll
    b L_tls_cb_abort
L_tls_cb_io:
    mov x1, x21
    ldr x2, [x19]
    ldr w0, [x20]
    cmp w22, #1
    b.ne L_tls_cb_write
    bl _read
    b L_tls_callback_result
L_tls_cb_write:
    bl _write
    b L_tls_callback_result
L_tls_cb_abort:
    str xzr, [x19]
    // A deadline overrun is terminal for this handshake.
    mov w0, #9806 // errSSLClosedAbort
    neg w0, w0
    b L_tls_cb_return
L_tls_callback_result:
    cmp x0, #0
    b.lt L_tls_cb_sys_error
    b.eq L_tls_cb_eof
    ldr x9, [x19]
    str x0, [x19]
    cmp x0, x9
    b.eq L_tls_cb_ok
    b L_tls_cb_short
L_tls_cb_sys_error:
    bl ___error
    ldr w9, [x0]
    cmp w9, #4 // EINTR: retry without losing the handshake deadline.
    b.eq L_tls_cb_interrupted
    cmp w9, #35 // EAGAIN / EWOULDBLOCK
    b.ne L_tls_cb_abort
    str xzr, [x19]
    b L_tls_cb_short
L_tls_cb_interrupted:
    ldr x23, [x20, #104]
    cbnz x23, L_tls_cb_poll
    b L_tls_cb_io
L_tls_cb_short:
    mov w0, #9803
    neg w0, w0
    b L_tls_cb_return
L_tls_cb_eof:
    str xzr, [x19]
    // TCP EOF without TLS close_notify is an aborted/truncated stream.
    mov w0, #9806 // errSSLClosedAbort
    neg w0, w0
    b L_tls_cb_return
L_tls_cb_ok:
    mov w0, #0
L_tls_cb_return:
    ldp x23, x30, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #80
    ret

// Use local text labels so both linkers preserve the exact addresses. The
// shared cstring section also contains f64 constants which are not strings.
.p2align 2
L_tls_security_path:  .asciz "/System/Library/Frameworks/Security.framework/Security"
L_tls_core_path:      .asciz "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation"
L_tls_create_name:    .asciz "SSLCreateContext"
L_tls_io_name:        .asciz "SSLSetIOFuncs"
L_tls_connection_name: .asciz "SSLSetConnection"
L_tls_domain_name:    .asciz "SSLSetPeerDomainName"
L_tls_break_name:     .asciz "SSLSetSessionOption"
L_tls_min_name:       .asciz "SSLSetProtocolVersionMin"
L_tls_handshake_name: .asciz "SSLHandshake"
L_tls_peer_trust_name: .asciz "SSLCopyPeerTrust"
L_tls_string_name:    .asciz "CFStringCreateWithBytes"
L_tls_policy_name:    .asciz "SecPolicyCreateSSL"
L_tls_set_policy_name: .asciz "SecTrustSetPolicies"
L_tls_evaluate_name:  .asciz "SecTrustEvaluateWithError"
L_tls_read_name:      .asciz "SSLRead"
L_tls_write_name:     .asciz "SSLWrite"
L_tls_shutdown_name:  .asciz "SSLClose"
L_tls_release_name:   .asciz "CFRelease"
.p2align 2
