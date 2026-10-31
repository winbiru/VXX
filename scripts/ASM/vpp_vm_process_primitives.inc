// VM process ABI (arena offsets, no host pointers accepted from guest).
// operation: 0 spawn, 1 poll, 2 read, 3 wait, 4 close.
// control (byte offsets): pid 0, stdout 4, stderr 8, argv_offsets 12,
// argv_pointers 16, argc 20, timeout 28, buffer 32, count 36,
// channel 40, errno 48. Child-only env offsets 24, count 44, envp scratch 56.
// Scratch at 64..; control requires 1024 bytes.
// Opaque POSIX spawn actions/attributes never leave this ASM boundary.
#if defined(__APPLE__)
#define VM_PROC_SYM(x) _##x
#define VM_PROC_LIB(x) _##x
#else
#define VM_PROC_SYM(x) x
#if defined(__x86_64__)
#define VM_PROC_LIB(x) x@PLT
#else
#define VM_PROC_LIB(x) x
#endif
#endif
.text
#if defined(__aarch64__) || defined(__arm64__)
.macro PROC_ENTER
    stp x19, x20, [sp, #-64]!
    stp x21, x22, [sp, #16]
    stp x23, x30, [sp, #32]
    mov w19, w0
    mov w22, w1
    bl VM_PROC_SYM(vpp_native_memory_base)
    mov x21, x0
    cbz x21, Lproc_invalid_arm
    // Bounds also prevent an overflow of the 1024-byte control block.
    cmp w22, #0
    b.lt Lproc_invalid_arm
    mov w10, #0x52c0
    movk w10, #0x3fe, lsl #16 // 67000000 - 1024 = 0x03fe 52c0
    cmp w22, w10
    b.hi Lproc_invalid_arm
    tst w22, #15
    b.ne Lproc_invalid_arm
    add x20, x21, w22, uxtw
.endm
.macro PROC_RETURN
    ldp x23, x30, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #64
    ret
.endm
.macro PROC_CALL name
    bl VM_PROC_LIB(\name)
.endm

.globl VM_PROC_SYM(vpp_native_os_process)
VM_PROC_SYM(vpp_native_os_process):
    PROC_ENTER
    cmp w19, #4
    b.hi Lproc_invalid_arm
    cbz w19, Lproc_spawn_arm
    cmp w19, #1
    b.eq Lproc_poll_arm
    cmp w19, #2
    b.eq Lproc_read_arm
    cmp w19, #3
    b.eq Lproc_wait_arm
    b Lproc_cleanup_arm

Lproc_spawn_arm:
    ldr w23, [x20, #20]
    cmp w23, #1
    b.lt Lproc_invalid_arm
    cmp w23, #128
    b.gt Lproc_invalid_arm
    ldr w10, [x20, #12] // argv offsets, array of i32
    ldr w11, [x20, #16] // argv pointers, array of u64
    cmp w10, #0
    b.lt Lproc_invalid_arm
    cmp w11, #0
    b.lt Lproc_invalid_arm
    tst w10, #3
    b.ne Lproc_invalid_arm
    tst w11, #7
    b.ne Lproc_invalid_arm
    mov w12, #0x54c0
    movk w12, #0x3fe, lsl #16 // 67000000 - 512
    cmp w10, w12
    b.hi Lproc_invalid_arm
    mov w12, #0x52b8
    movk w12, #0x3fe, lsl #16 // 67000000 - 1032
    cmp w11, w12
    b.hi Lproc_invalid_arm
    add x22, x21, w10, uxtw
    add x23, x21, w11, uxtw
    mov w9, #0
Lproc_argv_arm:
    ldr w10, [x20, #20]
    cmp w9, w10
    b.ge Lproc_argv_done_arm
    ldr w11, [x22, w9, uxtw #2]
    mov w12, #0x10
    movk w12, #0x80, lsl #16 // 8388624 (minimum heap payload)
    cmp w11, w12
    b.lo Lproc_invalid_arm
    mov w12, #0x46c0
    movk w12, #0x3fe, lsl #16 // 67000000 - 4096
    cmp w11, w12
    b.hi Lproc_invalid_arm
    add x13, x21, w11, uxtw
    str x13, [x23, w9, uxtw #3]
    // Ensure every argv entry is terminated within the designated 4096 bytes.
    // Loop over bytes here: libc calls would clobber our iteration registers.
    mov w14, #0
Lproc_arg_nul_arm:
    ldrb w15, [x13, w14, uxtw]
    cbz w15, Lproc_arg_ok_arm
    add w14, w14, #1
    cmp w14, #4096
    b.lo Lproc_arg_nul_arm
    b Lproc_invalid_arm
Lproc_arg_ok_arm:
    add w9, w9, #1
    b Lproc_argv_arm
Lproc_argv_done_arm:
    str xzr, [x23, w9, uxtw #3]
    str wzr, [x20, #48]
    mov w10, #-1
    stp w10, w10, [x20, #64]
    stp w10, w10, [x20, #72]
    str wzr, [x20, #88] // file_actions initialized
    str wzr, [x20, #92] // spawnattr initialized
    add x0, x20, #64
    PROC_CALL pipe
    cbnz w0, Lproc_errno_arm
    add x0, x20, #72
    PROC_CALL pipe
    cbnz w0, Lproc_errno_arm
    // Promote 0/1/2 before addclose so child stdout/stderr cannot be closed.
    mov w22, #0
Lproc_promote_arm:
    cmp w22, #4
    b.ge Lproc_cloexec_init_arm
    add x23, x20, #64
    ldr w0, [x23, w22, uxtw #2]
    cmp w0, #3
    b.ge Lproc_promote_next_arm
#if defined(__APPLE__)
    mov w1, #67              // F_DUPFD_CLOEXEC
#else
    mov w1, #1030
#endif
    mov w2, #3
    PROC_CALL fcntl
    cmp w0, #0
    b.lt Lproc_errno_arm
    mov w23, w0
    add x9, x20, #64
    ldr w0, [x9, w22, uxtw #2]
    PROC_CALL close
    add x9, x20, #64
    str w23, [x9, w22, uxtw #2]
Lproc_promote_next_arm:
    add w22, w22, #1
    b Lproc_promote_arm
Lproc_cloexec_init_arm:
    mov w22, #0
Lproc_cloexec_arm:
    add x9, x20, #64
    ldr w0, [x9, w22, uxtw #2]
    mov w1, #2               // F_SETFD
    mov w2, #1               // FD_CLOEXEC
    PROC_CALL fcntl
    cmp w0, #0
    b.lt Lproc_errno_arm
    add w22, w22, #1
    cmp w22, #4
    b.lt Lproc_cloexec_arm
    add x0, x20, #192
    PROC_CALL posix_spawn_file_actions_init
    cbnz w0, Lproc_error_rc_arm
    mov w9, #1
    str w9, [x20, #88]
    add x0, x20, #192
    ldr w1, [x20, #68]
    mov w2, #1
    PROC_CALL posix_spawn_file_actions_adddup2
    cbnz w0, Lproc_error_rc_arm
    add x0, x20, #192
    ldr w1, [x20, #76]
    mov w2, #2
    PROC_CALL posix_spawn_file_actions_adddup2
    cbnz w0, Lproc_error_rc_arm
    mov w22, #0
Lproc_addclose_arm:
    add x0, x20, #192
    add x9, x20, #64
    ldr w1, [x9, w22, uxtw #2]
    PROC_CALL posix_spawn_file_actions_addclose
    cbnz w0, Lproc_error_rc_arm
    add w22, w22, #1
    cmp w22, #4
    b.lt Lproc_addclose_arm
    add x0, x20, #320
    PROC_CALL posix_spawnattr_init
    cbnz w0, Lproc_error_rc_arm
    mov w9, #1
    str w9, [x20, #92]
    add x0, x20, #320
    mov w1, #2               // POSIX_SPAWN_SETPGROUP
    PROC_CALL posix_spawnattr_setflags
    cbnz w0, Lproc_error_rc_arm
    add x0, x20, #320
    mov w1, #0
    PROC_CALL posix_spawnattr_setpgroup
    cbnz w0, Lproc_error_rc_arm
    // Build the child environment without mutating the parent's environ.
    // V++ validates KEY=VALUE and owns raw offsets; this boundary merges them
    // with inherited entries, replacing only the matching keys.
#if defined(__APPLE__)
    PROC_CALL _NSGetEnviron
    ldr x5, [x0]
#else
    adrp x9, :got:environ
    ldr x9, [x9, :got_lo12:environ]
    ldr x5, [x9]
#endif
    ldr w9, [x20, #44]       // child override count
    cbz w9, Lproc_env_ready_arm
    cmp w9, #128
    b.hi Lproc_bad_env_arm
    ldr w10, [x20, #24]     // i32 override offsets
    ldr w11, [x20, #56]     // u64 merged envp pointers
    tst w10, #3
    b.ne Lproc_bad_env_arm
    tst w11, #7
    b.ne Lproc_bad_env_arm
    mov w12, #0x54c0
    movk w12, #0x3fe, lsl #16
    cmp w10, w12
    b.hi Lproc_bad_env_arm
    // Use the conservative 33824-byte allocation bound.
    mov w12, #0xd2a0       // 67000000 - 33824 = 0x03fd d2a0
    movk w12, #0x3fd, lsl #16
    cmp w11, w12
    b.hi Lproc_bad_env_arm
    add x22, x21, w10, uxtw
    add x23, x21, w11, uxtw
    mov w16, #0             // inherited index
    mov w17, #0             // merged output index
Lproc_env_inherited_arm:
    cmp w16, #4096
    b.hs Lproc_bad_env_arm
    ldr x9, [x5, w16, uxtw #3]
    cbz x9, Lproc_env_append_arm
    mov w10, #0             // override index
Lproc_env_check_override_arm:
    ldr w11, [x20, #44]
    cmp w10, w11
    b.ge Lproc_env_keep_arm
    ldr w12, [x22, w10, uxtw #2]
    mov w13, #0x10
    movk w13, #0x80, lsl #16
    cmp w12, w13
    b.lo Lproc_bad_env_arm
    mov w13, #0x46c0
    movk w13, #0x3fe, lsl #16
    cmp w12, w13
    b.hi Lproc_bad_env_arm
    add x11, x21, w12, uxtw
    mov w12, #0
Lproc_env_compare_arm:
    ldrb w13, [x11, w12, uxtw]
    ldrb w14, [x9, w12, uxtw]
    cmp w13, #61            // '=' separates the key
    b.eq Lproc_env_key_end_arm
    cmp w13, w14
    b.ne Lproc_env_next_override_arm
    add w12, w12, #1
    cmp w12, #4096
    b.lo Lproc_env_compare_arm
    b Lproc_bad_env_arm
Lproc_env_key_end_arm:
    cmp w14, #61
    b.eq Lproc_env_skip_arm
Lproc_env_next_override_arm:
    add w10, w10, #1
    b Lproc_env_check_override_arm
Lproc_env_keep_arm:
    str x9, [x23, w17, uxtw #3]
    add w17, w17, #1
Lproc_env_skip_arm:
    add w16, w16, #1
    b Lproc_env_inherited_arm
Lproc_env_append_arm:
    mov w10, #0
Lproc_env_add_override_arm:
    ldr w11, [x20, #44]
    cmp w10, w11
    b.ge Lproc_env_terminate_arm
    ldr w12, [x22, w10, uxtw #2]
    mov w13, #0x10
    movk w13, #0x80, lsl #16
    cmp w12, w13
    b.lo Lproc_bad_env_arm
    mov w13, #0x46c0
    movk w13, #0x3fe, lsl #16
    cmp w12, w13
    b.hi Lproc_bad_env_arm
    add x12, x21, w12, uxtw
    str x12, [x23, w17, uxtw #3]
    add w17, w17, #1
    add w10, w10, #1
    b Lproc_env_add_override_arm
Lproc_env_terminate_arm:
    str xzr, [x23, w17, uxtw #3]
    mov x5, x23
Lproc_env_ready_arm:
    add x0, x20, #0
    ldr w9, [x20, #16]
    add x4, x21, w9, uxtw
    ldr x1, [x4]
    add x2, x20, #192
    add x3, x20, #320
    PROC_CALL posix_spawnp
    cbnz w0, Lproc_error_rc_arm
    mov w9, #1
    str w9, [x20, #96]       // spawn succeeded
    b Lproc_finalize_arm
Lproc_bad_env_arm:
    mov w0, #22
    b Lproc_error_rc_arm
Lproc_errno_arm:
#if defined(__APPLE__)
    PROC_CALL __error
#else
    PROC_CALL __errno_location
#endif
    ldr w0, [x0]
Lproc_error_rc_arm:
    str w0, [x20, #48]
    str wzr, [x20, #96]
Lproc_finalize_arm:
    ldr w9, [x20, #92]
    cbz w9, 1f
    add x0, x20, #320
    PROC_CALL posix_spawnattr_destroy
1:
    ldr w9, [x20, #88]
    cbz w9, 2f
    add x0, x20, #192
    PROC_CALL posix_spawn_file_actions_destroy
2:
    ldr w0, [x20, #68]
    PROC_CALL close
    ldr w0, [x20, #76]
    PROC_CALL close
    mov w0, #-1
    str w0, [x20, #68]
    str w0, [x20, #76]
    ldr w9, [x20, #96]
    cbz w9, Lproc_spawn_fail_arm
    ldr w9, [x20, #64]
    str w9, [x20, #4]
    ldr w9, [x20, #72]
    str w9, [x20, #8]
    mov w0, #1
    PROC_RETURN
Lproc_spawn_fail_arm:
    ldr w0, [x20, #64]
    cmp w0, #0
    b.lt 3f
    PROC_CALL close
3:
    ldr w0, [x20, #72]
    cmp w0, #0
    b.lt 4f
    PROC_CALL close
4:
    mov w0, #0
    PROC_RETURN

Lproc_poll_arm:
    mov w22, #0
    ldr w9, [x20, #4]
    cmp w9, #0
    b.lt 1f
    str w9, [x20, #64]
    mov w10, #1
    strh w10, [x20, #68]
    strh wzr, [x20, #70]
    add w22, w22, #1
1:
    ldr w9, [x20, #8]
    cmp w9, #0
    b.lt 2f
    add x10, x20, #64
    add x10, x10, w22, uxtw #3
    str w9, [x10]
    mov w9, #1
    strh w9, [x10, #4]
    strh wzr, [x10, #6]
    add w22, w22, #1
2:
    cbz w22, Lproc_poll_both_eof_arm
    add x0, x20, #64
    mov w1, w22
    ldr w2, [x20, #28]
    mov w9, #5000
    cmp w2, w9
    b.hi Lproc_invalid_arm
    PROC_CALL poll
    cmp w0, #0
    b.lt Lproc_io_errno_arm
    mov w9, #0
    mov w10, #0
    ldr w11, [x20, #4]
    cmp w11, #0
    b.lt 3f
    ldrh w12, [x20, #70]
    mov w13, #0x39
    tst w12, w13          // POLLIN|POLLERR|POLLHUP|POLLNVAL
    cset w13, ne
    orr w9, w9, w13
    add w10, w10, #1
3:
    ldr w11, [x20, #8]
    cmp w11, #0
    b.lt 4f
    add x12, x20, #64
    add x12, x12, w10, uxtw #3
    ldrh w12, [x12, #6]
    mov w13, #0x39
    tst w12, w13
    cset w13, ne
    orr w9, w9, w13, lsl #1
4:
    mov w0, w9
    PROC_RETURN
Lproc_poll_both_eof_arm:
    mov w0, #4
    PROC_RETURN

Lproc_read_arm:
    ldr w22, [x20, #40]
    cmp w22, #1
    b.eq 1f
    cmp w22, #2
    b.ne Lproc_invalid_arm
1:
    ldr w23, [x20, #36]
    cmp w23, #1
    b.lt Lproc_invalid_arm
    cmp w23, #4096
    b.gt Lproc_invalid_arm
    ldr w9, [x20, #32]
    cmp w9, #0
    b.lt Lproc_invalid_arm
    mov w10, #0x56c0
    movk w10, #0x3fe, lsl #16
    sub w10, w10, w23
    cmp w9, w10
    b.hi Lproc_invalid_arm
    add x23, x21, w9, uxtw
    add x22, x20, w22, uxtw #2
    ldr w0, [x22]
    cmp w0, #0
    b.lt Lproc_read_eof_arm
Lproc_read_retry_arm:
    mov x1, x23
    ldr w2, [x20, #36]
    PROC_CALL read
    cmp x0, #0
    b.gt Lproc_return_arm
    b.eq Lproc_read_close_arm
#if defined(__APPLE__)
    PROC_CALL __error
#else
    PROC_CALL __errno_location
#endif
    ldr w9, [x0]
    cmp w9, #4
    b.eq Lproc_read_retry_arm
    neg w0, w9
    PROC_RETURN
Lproc_read_close_arm:
    ldr w0, [x22]
    PROC_CALL close
    mov w9, #-1
    str w9, [x22]
Lproc_read_eof_arm:
    mov w0, #0
    PROC_RETURN

Lproc_wait_arm:
    ldr w22, [x20]
    cmp w22, #0
    b.le Lproc_invalid_arm
    ldr w9, [x20, #4]
    cmp w9, #0
    b.ge Lproc_invalid_arm
    ldr w9, [x20, #8]
    cmp w9, #0
    b.ge Lproc_invalid_arm
Lproc_wait_retry_arm:
    mov w0, w22
    add x1, x20, #100
    mov w2, #0
    PROC_CALL waitpid
    cmp w0, #0
    b.gt Lproc_wait_exit_arm
#if defined(__APPLE__)
    PROC_CALL __error
#else
    PROC_CALL __errno_location
#endif
    ldr w9, [x0]
    cmp w9, #4
    b.eq Lproc_wait_retry_arm
    neg w0, w9
    PROC_RETURN
Lproc_wait_exit_arm:
    str wzr, [x20]           // disarm future process-group cleanup
    ldr w9, [x20, #100]
    and w10, w9, #127
    cbz w10, Lproc_wait_normal_arm
    cmp w10, #127
    b.eq Lproc_invalid_arm
    add w0, w10, #128
    PROC_RETURN
Lproc_wait_normal_arm:
    lsr w0, w9, #8
    and w0, w0, #255
    PROC_RETURN

Lproc_cleanup_arm:
    ldr w0, [x20, #4]
    cmp w0, #0
    b.lt 1f
    PROC_CALL close
1:
    ldr w0, [x20, #8]
    cmp w0, #0
    b.lt 2f
    PROC_CALL close
2:
    mov w9, #-1
    str w9, [x20, #4]
    str w9, [x20, #8]
    ldr w22, [x20]
    str wzr, [x20]
    cmp w22, #0
    b.le Lproc_cleanup_done_arm
    neg w0, w22
    mov w1, #9
    PROC_CALL kill
3:
    mov w0, w22
    add x1, x20, #100
    mov w2, #0
    PROC_CALL waitpid
    cmp w0, #0
    b.ge Lproc_cleanup_done_arm
#if defined(__APPLE__)
    PROC_CALL __error
#else
    PROC_CALL __errno_location
#endif
    ldr w9, [x0]
    cmp w9, #4
    b.eq 3b
Lproc_cleanup_done_arm:
    mov w0, #0
    PROC_RETURN

Lproc_io_errno_arm:
#if defined(__APPLE__)
    PROC_CALL __error
#else
    PROC_CALL __errno_location
#endif
    ldr w0, [x0]
    // EINTR during poll is a wake-up. The V++ policy checks its monotonic
    // deadline before issuing the next poll; no process state is lost.
    cmp w0, #4
    b.eq Lproc_poll_interrupted_arm
    neg w0, w0
Lproc_return_arm:
    PROC_RETURN
Lproc_poll_interrupted_arm:
    mov w0, #0
    PROC_RETURN
Lproc_invalid_arm:
    mov w0, #-22
    PROC_RETURN

#else
#if defined(__x86_64__)
#include "vpp_vm_process_x86_64.inc"
#else
#error Unsupported standalone VM process architecture
#endif
#endif
