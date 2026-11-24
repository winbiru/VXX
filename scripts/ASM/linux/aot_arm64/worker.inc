// Linux AArch64 ELF worker runtime. Ported from the macOS AAPCS64 implementation.
// Native worker transport. Records are host-only pointers, never guest i32s.
// Layout: entry 0, context 8, OS thread 16, arena 24, state 32,
// terminal arbitration 36 (0 active, 1 cancel won, 2 completion won),
// result 40, joined 44. The registry owns the record until join.
// Entry(record, context) -> i32; trusted guest callbacks compact their result.
// record+60 = compact snapshot length (zero for an ordinary arena).
// A single registry owner must serialize join/dispose with readers.
.text
.p2align 2
// Snapshot buffers occupy disjoint mappings. Copy in native AArch64 without
// the C memcpy ABI. The caller owns the source and destination lifetimes.
Llinux_worker_memcpy:
    mov x9, x0
1:  cmp x2, #16
    b.lo 2f
    ldp x10, x11, [x1], #16
    stp x10, x11, [x9], #16
    sub x2, x2, #16
    b 1b
2:  cbz x2, 3f
    ldrb w10, [x1], #1
    strb w10, [x9], #1
    subs x2, x2, #1
    b.ne 2b
3:  ret

.globl vpp_native_worker_start
vpp_native_worker_start:
    cbz x0, Lworker_start_invalid
    stp x19, x20, [sp, #-32]!
    stp x21, x30, [sp, #16]
    mov x19, x0
    mov x20, x1
    bl vpp_native_arena_enable_threads
    cbnz w0, Lworker_start_failed
    mov x0, #0
    mov x1, #64
    mov x2, #3
    mov x3, #0x22
    mov x4, #-1
    mov x5, #0
    bl Llinux_arena_mmap
    cmn x0, #1
    b.eq Lworker_start_failed
    mov x21, x0
    stp x19, x20, [x21]
    add x0, x21, #16
    mov x1, #0
    adrp x2, :got:vpp_worker_entry_callback
    ldr x2, [x2, :got_lo12:vpp_worker_entry_callback]
    // GOT address already resolved in x2
    mov x3, x21
    bl pthread_create
    cbnz w0, Lworker_start_release
    mov x0, x21
    b Lworker_start_return
Lworker_start_release:
    mov x0, x21
    mov x1, #64
    bl Llinux_arena_munmap
Lworker_start_failed:
    mov x0, #0
Lworker_start_return:
    ldp x21, x30, [sp, #16]
    ldp x19, x20, [sp], #32
    ret
Lworker_start_invalid:
    mov x0, #0
    ret

.p2align 2
vpp_worker_entry_callback:
    stp x19, x30, [sp, #-16]!
    mov x19, x0
    bl vpp_native_memory_base
    cbz x0, Lworker_entry_failed
    str x0, [x19, #24]
    adrp x9, :got:vpp_native_worker_tls_key
    ldr x9, [x9, :got_lo12:vpp_native_worker_tls_key]
    ldr x0, [x9]
    mov x1, x19
    bl pthread_setspecific
    cbnz w0, Lworker_entry_failed
    ldp x9, x1, [x19]
    mov x0, x19
    blr x9
    str w0, [x19, #40]
    mov w10, #1
    b Lworker_entry_publish
Lworker_entry_failed:
    mov w10, #-1
Lworker_entry_publish:
    // Seal completion against cancellation before publishing arena/result.
    add x9, x19, #36
1:  ldaxr w11, [x9]
    cbnz w11, 2f
    mov w11, #2
    stlxr w12, w11, [x9]
    cbnz w12, 1b
    b 3f
2:  clrex
3:  add x9, x19, #32
    stlr w10, [x9]
    mov x0, #0
    ldp x19, x30, [sp], #16
    ret

.globl vpp_native_worker_state
vpp_native_worker_state:
    add x9, x0, #32
    ldar w0, [x9]
    ret

.globl vpp_native_worker_request_cancel
vpp_native_worker_request_cancel:
    add x9, x0, #36
1:  ldaxr w10, [x9]
    cbnz w10, 2f
    mov w10, #1
    stlxr w11, w10, [x9]
    cbnz w11, 1b
    mov w0, #1
    ret
2:  clrex
    mov w0, #0
    ret

.globl vpp_native_worker_cancelled
vpp_native_worker_cancelled:
    add x9, x0, #36
    ldar w0, [x9]
    cmp w0, #1
    cset w0, eq
    ret

// Join once, by registry owner. Do not release the arena if joining fails.
.globl vpp_native_worker_join
vpp_native_worker_join:
    stp x19, x30, [sp, #-16]!
    mov x19, x0
    ldr w9, [x19, #44]
    cbnz w9, Lworker_join_done
    ldr x0, [x19, #16]
    mov x1, #0
    bl pthread_join
    cbnz w0, Lworker_join_return
    mov w9, #1
    str w9, [x19, #44]
Lworker_join_done:
    mov w0, #0
Lworker_join_return:
    ldp x19, x30, [sp], #16
    ret

// Caller holds record lifetime; read only after completion (acquire).
// Returns 0 on success and writes a 32-bit arena word to host output.
.globl vpp_native_worker_read_i32
vpp_native_worker_read_i32:
    add x9, x0, #32
    ldar w10, [x9]
    cmp w10, #1
    b.ne Lworker_read_failed
    mov w10, #0x13fc
    movk w10, #0x810, lsl #16
    cmp w1, w10
    b.hi Lworker_read_failed
    cbz x2, Lworker_read_failed
    ldr x9, [x0, #24]
    cbz x9, Lworker_read_failed
    ldr w10, [x0, #60]
    cbz w10, 1f
    ldr w11, [x0, #40]
    cmp w11, #0
    cneg w11, w11, lt
    subs w1, w1, w11
    b.lo Lworker_read_failed
    sub w10, w10, #4
    cmp w1, w10
    b.hi Lworker_read_failed
1:  ldr w10, [x9, w1, uxtw]
    str w10, [x2]
    mov w0, #0
    ret
Lworker_read_failed:
    mov w0, #-1
    ret

.globl vpp_native_worker_result
vpp_native_worker_result:
    add x9, x0, #32
    ldar w10, [x9]
    cmp w10, #1
    b.ne Lworker_read_failed
    cbz x1, Lworker_read_failed
    ldr w10, [x0, #40]
    str w10, [x1]
    mov w0, #0
    ret

.globl vpp_native_worker_dispose
vpp_native_worker_dispose:
    stp x19, x30, [sp, #-16]!
    mov x19, x0
    ldr w9, [x19, #44]
    cbz w9, Lworker_dispose_failed
    ldr x0, [x19, #24]
    cbz x0, Lworker_dispose_record
    ldr w1, [x19, #60]
    cbnz w1, 1f
    mov x1, #0x1400
    movk x1, #0x810, lsl #16
1:  bl Llinux_arena_munmap
    cbnz w0, Lworker_dispose_return
    str xzr, [x19, #24]
Lworker_dispose_record:
    mov x0, x19
    mov x1, #64
    bl Llinux_arena_munmap
    b Lworker_dispose_return
Lworker_dispose_failed:
    mov w0, #-1
Lworker_dispose_return:
    ldp x19, x30, [sp], #16
    ret

// Called only after a trusted guest entry returns, before terminal publication.
// Keep the original signed result offset for the guest ABI, but replace the
// private arena with a compact immutable packet. Raw callbacks never call this.
// On allocation or unmap failure, retain the original arena for disposal.
.globl vpp_native_worker_compact_result
vpp_native_worker_compact_result:
    stp x19, x20, [sp, #-64]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    str x30, [sp, #48]
    mov x19, x0
    mov w20, w1
    ldr x21, [x19, #24]
    cbz x21, Lworker_compact_return
    mov x22, #0
    mov w23, #0
    cmn w20, #1
    b.eq Lworker_compact_release
    cmp w20, #0
    cneg w24, w20, lt
    mov w9, #16
    movk w9, #128, lsl #16 // 8388624
    cmp w24, w9
    b.lo Lworker_compact_return
    mov w9, #0x0df0
    movk w9, #0x727, lsl #16 // 120000000 - 16
    cmp w24, w9
    b.hi Lworker_compact_return
    add x24, x21, w24, uxtw
    ldp w9, w23, [x24]
    mov w10, #0x5001
    movk w10, #0x5750, lsl #16 // snapshot magic
    cmp w9, w10
    b.ne Lworker_compact_return
    cmp w23, #16
    b.lo Lworker_compact_return
    mov w9, #0x05c0
    movk w9, #0x1d9, lsl #16
    cmp w23, w9
    b.hi Lworker_compact_return
    sub x9, x24, x21
    add x9, x9, w23, uxtw
    mov w10, #0x0e00
    movk w10, #0x727, lsl #16
    cmp x9, x10
    b.hi Lworker_compact_return
    mov x0, #0
    mov w1, w23
    mov x2, #3
    mov x3, #0x22
    mov x4, #-1
    mov x5, #0
    bl Llinux_arena_mmap
    cmn x0, #1
    b.eq Lworker_compact_return
    mov x22, x0
    mov x1, x24
    mov w2, w23
    bl Llinux_worker_memcpy
Lworker_compact_release:
    adrp x9, :got:vpp_native_arena_tls_key
    ldr x9, [x9, :got_lo12:vpp_native_arena_tls_key]
    ldr x0, [x9]
    mov x1, #0
    bl pthread_setspecific
    cbnz w0, Lworker_compact_fallback
    mov x0, x21
    mov x1, #0x1400
    movk x1, #0x810, lsl #16
    bl Llinux_arena_munmap
    cbnz w0, Lworker_compact_fallback
    str x22, [x19, #24]
    str w23, [x19, #60]
    b Lworker_compact_return
Lworker_compact_fallback:
    cbz x22, Lworker_compact_return
    mov x0, x22
    mov w1, w23
    bl Llinux_arena_munmap
Lworker_compact_return:
    mov w0, w20
    ldr x30, [sp, #48]
    ldp x23, x24, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #64
    ret

// Root-owned registry. Handles are one-based indices, never host pointers.
// Slots remain live until the root closes the registry and joins every thread.
// record+48 = handle, record+52 = dependency handle (both under registry lock).
// Shutdown joins active workers outside the lock. Terminal reaping joins
// under the lock only after the callback has returned and published its state.
.bss
.p2align 2
vpp_worker_registry_lock: .skip 4
.text
.bss
.p2align 2
vpp_worker_registry_count: .skip 4
.text
.bss
.p2align 2
vpp_worker_registry_closing: .skip 4
.text
.bss
.p2align 3
vpp_worker_registry_slots: .skip 32768
.text
.text
.p2align 2
Lworker_registry_lock:
    adrp x9, :got:vpp_worker_registry_lock
    ldr x9, [x9, :got_lo12:vpp_worker_registry_lock]
    // GOT address already resolved in x9
1:  ldaxr w10, [x9]
    cbnz w10, 1b
    mov w10, #1
    stxr w11, w10, [x9]
    cbnz w11, 1b
    ret
Lworker_registry_unlock:
    adrp x9, :got:vpp_worker_registry_lock
    ldr x9, [x9, :got_lo12:vpp_worker_registry_lock]
    // GOT address already resolved in x9
    stlr wzr, [x9]
    ret

// Locked lookup. w0 = positive handle. x0 = record or null.
Lworker_registry_find:
    cmp w0, #1
    b.lt 1f
    adrp x9, :got:vpp_worker_registry_count
    ldr x9, [x9, :got_lo12:vpp_worker_registry_count]
    ldr w10, [x9]
    cmp w0, w10
    b.gt 1f
    sub w0, w0, #1
    adrp x9, :got:vpp_worker_registry_slots
    ldr x9, [x9, :got_lo12:vpp_worker_registry_slots]
    // GOT address already resolved in x9
    ldr x0, [x9, w0, uxtw #3]
    ret
1:  mov x0, #0
    ret

.globl vpp_native_worker_registry_start
vpp_native_worker_registry_start:
    stp x19, x20, [sp, #-48]!
    stp x21, x22, [sp, #16]
    str x30, [sp, #32]
    mov x19, x0
    mov x20, x1
    bl Lworker_registry_lock
    adrp x9, :got:vpp_worker_registry_closing
    ldr x9, [x9, :got_lo12:vpp_worker_registry_closing]
    ldr w9, [x9]
    cbnz w9, Lworker_registry_start_fail
    adrp x21, :got:vpp_worker_registry_count
    ldr x21, [x21, :got_lo12:vpp_worker_registry_count]
    // GOT address already resolved in x21
    ldr w22, [x21]
    cmp w22, #4096
    b.ge Lworker_registry_start_fail
    mov x0, x19
    mov x1, x20
    bl vpp_native_worker_start
    cbz x0, Lworker_registry_start_fail
    adrp x9, :got:vpp_worker_registry_slots
    ldr x9, [x9, :got_lo12:vpp_worker_registry_slots]
    // GOT address already resolved in x9
    str x0, [x9, w22, uxtw #3]
    add w22, w22, #1
    str w22, [x0, #48]
    str w22, [x21]
    b Lworker_registry_start_done
Lworker_registry_start_fail:
    mov w22, #-1
Lworker_registry_start_done:
    bl Lworker_registry_unlock
    mov w0, w22
    ldr x30, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #48
    ret

.globl vpp_native_worker_registry_record
vpp_native_worker_registry_record:
    stp x19, x30, [sp, #-16]!
    bl Lworker_registry_lock
    bl Lworker_registry_find
    mov x19, x0
    bl Lworker_registry_unlock
    mov x0, x19
    ldp x19, x30, [sp], #16
    ret

// The callback may run before registry_start publishes record+48.
.globl vpp_native_worker_registry_self
vpp_native_worker_registry_self:
    stp x19, x30, [sp, #-16]!
    mov x19, x0
    bl Lworker_registry_lock
    ldr w19, [x19, #48]
    bl Lworker_registry_unlock
    mov w0, w19
    ldp x19, x30, [sp], #16
    ret

// Returns 0 on success, -1 invalid handle, -2 self/cyclic/already waiting.
// Root (self=0) has no dependency edge: no worker can wait on it.
.globl vpp_native_worker_registry_wait_begin
vpp_native_worker_registry_wait_begin:
    stp x19, x20, [sp, #-48]!
    stp x21, x22, [sp, #16]
    str x30, [sp, #32]
    mov w19, w0
    mov w20, w1
    bl Lworker_registry_lock
    mov w0, w20
    bl Lworker_registry_find
    cbz x0, Lworker_registry_wait_invalid
    mov x21, x0
    cbz w19, Lworker_registry_wait_ok
    cmp w19, w20
    b.eq Lworker_registry_wait_cycle
    mov w0, w19
    bl Lworker_registry_find
    cbz x0, Lworker_registry_wait_invalid
    mov x22, x0
    ldr w9, [x22, #52]
    cbnz w9, Lworker_registry_wait_cycle
Lworker_registry_wait_walk:
    ldr w0, [x21, #52]
    cbz w0, Lworker_registry_wait_commit
    cmp w0, w19
    b.eq Lworker_registry_wait_cycle
    bl Lworker_registry_find
    cbz x0, Lworker_registry_wait_invalid
    mov x21, x0
    b Lworker_registry_wait_walk
Lworker_registry_wait_commit:
    str w20, [x22, #52]
Lworker_registry_wait_ok:
    mov w19, #0
    b Lworker_registry_wait_return
Lworker_registry_wait_invalid:
    mov w19, #-1
    b Lworker_registry_wait_return
Lworker_registry_wait_cycle:
    mov w19, #-2
Lworker_registry_wait_return:
    bl Lworker_registry_unlock
    mov w0, w19
    ldr x30, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #48
    ret

.globl vpp_native_worker_registry_wait_end
vpp_native_worker_registry_wait_end:
    cbz w0, 1f
    stp x19, x30, [sp, #-16]!
    bl Lworker_registry_lock
    bl Lworker_registry_find
    cbz x0, 2f
    str wzr, [x0, #52]
2:  bl Lworker_registry_unlock
    ldp x19, x30, [sp], #16
1:  ret

// Only the root calls shutdown. Closing prevents nested spawns from racing
// with teardown. If join fails, retain all records for a possible retry.
.globl vpp_native_worker_registry_shutdown
vpp_native_worker_registry_shutdown:
    stp x19, x20, [sp, #-48]!
    stp x21, x22, [sp, #16]
    str x30, [sp, #32]
    bl Lworker_registry_lock
    adrp x9, :got:vpp_worker_registry_closing
    ldr x9, [x9, :got_lo12:vpp_worker_registry_closing]
    mov w10, #1
    str w10, [x9]
    adrp x9, :got:vpp_worker_registry_count
    ldr x9, [x9, :got_lo12:vpp_worker_registry_count]
    ldr w19, [x9]
    adrp x20, :got:vpp_worker_registry_slots
    ldr x20, [x20, :got_lo12:vpp_worker_registry_slots]
    // GOT address already resolved in x20
    mov w21, #0
Lworker_registry_shutdown_cancel:
    cmp w21, w19
    b.ge Lworker_registry_shutdown_unlock
    ldr x0, [x20, w21, uxtw #3]
    cbz x0, 1f
    bl vpp_native_worker_request_cancel
1:  add w21, w21, #1
    b Lworker_registry_shutdown_cancel
Lworker_registry_shutdown_unlock:
    bl Lworker_registry_unlock
    mov w21, #0
Lworker_registry_shutdown_join:
    cmp w21, w19
    b.ge Lworker_registry_shutdown_dispose_begin
    ldr x0, [x20, w21, uxtw #3]
    cbz x0, 1f
    bl vpp_native_worker_join
    cbnz w0, Lworker_registry_shutdown_fail
1:  add w21, w21, #1
    b Lworker_registry_shutdown_join
Lworker_registry_shutdown_dispose_begin:
    mov w21, #0
Lworker_registry_shutdown_dispose:
    cmp w21, w19
    b.ge Lworker_registry_shutdown_done
    ldr x0, [x20, w21, uxtw #3]
    cbz x0, 1f
    bl vpp_native_worker_dispose
    cbnz w0, Lworker_registry_shutdown_fail
    str xzr, [x20, w21, uxtw #3]
1:  add w21, w21, #1
    b Lworker_registry_shutdown_dispose
Lworker_registry_shutdown_done:
    bl Lworker_registry_lock
    adrp x9, :got:vpp_worker_registry_count
    ldr x9, [x9, :got_lo12:vpp_worker_registry_count]
    str wzr, [x9]
    adrp x9, :got:vpp_worker_service_entry
    ldr x9, [x9, :got_lo12:vpp_worker_service_entry]
    str xzr, [x9]
    bl Lworker_registry_unlock
    mov w0, #0
    b Lworker_registry_shutdown_return
Lworker_registry_shutdown_fail:
    mov w0, #-1
Lworker_registry_shutdown_return:
    ldr x30, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #48
    ret

// A new root may open the empty registry after the previous root shut down.
.globl vpp_native_worker_registry_open
vpp_native_worker_registry_open:
    stp x19, x30, [sp, #-16]!
    bl Lworker_registry_lock
    adrp x9, :got:vpp_worker_registry_count
    ldr x9, [x9, :got_lo12:vpp_worker_registry_count]
    ldr w19, [x9]
    cbnz w19, 1f
    adrp x9, :got:vpp_worker_registry_closing
    ldr x9, [x9, :got_lo12:vpp_worker_registry_closing]
    str wzr, [x9]
    b 2f
1:  mov w19, #-1
2:  bl Lworker_registry_unlock
    mov w0, w19
    ldp x19, x30, [sp], #16
    ret

// Snapshot transport. start_copy(entry, source, length, destination) takes
// arena offsets, not guest pointers. The spawning thread stays suspended until
// the input bytes have been copied. The callback gets the destination offset.
// The temporary packet is on the caller's stack and is never used after the
// release publication at record+56. It is safe even if the child finishes first.
.globl vpp_native_worker_registry_start_copy
vpp_native_worker_registry_start_copy:
    cbz x0, Lworker_copy_invalid
    mov w9, #0x1400
    movk w9, #0x810, lsl #16
    cmp w2, w9
    b.hi Lworker_copy_invalid
    sub w9, w9, w2
    cmp w1, w9
    b.hi Lworker_copy_invalid
    cmp w3, w9
    b.hi Lworker_copy_invalid
    stp x19, x20, [sp, #-80]!
    stp x21, x22, [sp, #16]
    str x30, [sp, #32]
    // packet: callback, source native pointer, length, destination offset.
    str x0, [sp, #48]
    str w2, [sp, #64]
    str w3, [sp, #68]
    mov w20, w1
    bl vpp_native_memory_base
    cbz x0, Lworker_start_copy_fail
    add x0, x0, w20, uxtw
    str x0, [sp, #56]
    adrp x0, :got:vpp_worker_copy_entry_callback
    ldr x0, [x0, :got_lo12:vpp_worker_copy_entry_callback]
    // GOT address already resolved in x0
    add x1, sp, #48
    bl vpp_native_worker_registry_start
    cmp w0, #0
    b.le Lworker_start_copy_fail
    mov w19, w0
    bl vpp_native_worker_registry_record
    mov x20, x0
    add x21, x20, #56
    add x22, x20, #32
Lworker_start_copy_wait:
    ldar w9, [x21]
    cbnz w9, Lworker_start_copy_done
    ldar w9, [x22]
    cbz w9, 1f
    // Completion may have raced the first readiness load.
    ldar w9, [x21]
    cbnz w9, Lworker_start_copy_done
    str xzr, [x20, #8]
    b Lworker_start_copy_fail
1:  yield
    b Lworker_start_copy_wait
Lworker_start_copy_done:
    mov w0, w19
    b Lworker_start_copy_return
Lworker_start_copy_fail:
    mov w0, #-1
Lworker_start_copy_return:
    ldr x30, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #80
    ret
Lworker_copy_invalid:
    mov w0, #-1
    ret

vpp_worker_copy_entry_callback:
    stp x19, x20, [sp, #-48]!
    stp x21, x30, [sp, #16]
    mov x19, x0
    ldp x20, x9, [x1]
    ldp w2, w21, [x1, #16]
    ldr x0, [x19, #24]
    add x0, x0, w21, uxtw
    mov x1, x9
    bl Llinux_worker_memcpy
    // No further access to the parent's packet or source arena.
    str xzr, [x19, #8]
    add x9, x19, #56
    mov w10, #1
    stlr w10, [x9]
    mov x0, x19
    mov w1, w21
    blr x20
    ldp x21, x30, [sp, #16]
    ldp x19, x20, [sp], #48
    ret

// copy_result(handle, source, length, destination) copies only a completed
// worker's immutable arena into the caller's arena. Multiple readers are safe;
// the root keeps records alive until all workers have been joined.
.globl vpp_native_worker_registry_copy_result
vpp_native_worker_registry_copy_result:
    mov w9, #0x1400
    movk w9, #0x810, lsl #16
    cmp w2, w9
    b.hi Lworker_copy_invalid
    sub w9, w9, w2
    cmp w1, w9
    b.hi Lworker_copy_invalid
    cmp w3, w9
    b.hi Lworker_copy_invalid
    stp x19, x20, [sp, #-48]!
    stp x21, x22, [sp, #16]
    str x30, [sp, #32]
    mov w19, w1
    mov w20, w2
    mov w21, w3
    bl vpp_native_worker_registry_record
    cbz x0, Lworker_copy_result_fail
    add x9, x0, #32
    ldar w10, [x9]
    cmp w10, #1
    b.ne Lworker_copy_result_fail
    ldr x22, [x0, #24]
    cbz x22, Lworker_copy_result_fail
    ldr w9, [x0, #60]
    cbz w9, 1f
    ldr w10, [x0, #40]
    cmp w10, #0
    cneg w10, w10, lt
    subs w19, w19, w10
    b.lo Lworker_copy_result_fail
    subs w9, w9, w20
    b.lo Lworker_copy_result_fail
    cmp w19, w9
    b.hi Lworker_copy_result_fail
1:  bl vpp_native_memory_base
    cbz x0, Lworker_copy_result_fail
    add x0, x0, w21, uxtw
    add x1, x22, w19, uxtw
    mov w2, w20
    bl Llinux_worker_memcpy
    mov w0, #0
    b Lworker_copy_result_return
Lworker_copy_result_fail:
    mov w0, #-1
Lworker_copy_result_return:
    ldr x30, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #48
    ret

// Native TLS identity is deliberately outside the guest arena. It survives
// JIT arena swaps and cannot be forged by copying guest data.
.globl vpp_native_worker_current
vpp_native_worker_current:
    adrp x9, :got:vpp_native_arena_tls_enabled
    ldr x9, [x9, :got_lo12:vpp_native_arena_tls_enabled]
    ldr w9, [x9]
    cbz w9, 1f
    adrp x9, :got:vpp_native_worker_tls_key
    ldr x9, [x9, :got_lo12:vpp_native_worker_tls_key]
    ldr x0, [x9]
    b pthread_getspecific
1:  mov x0, #0
    ret

// Reap observed terminal threads once, serialized with other observers and
// root shutdown. A terminal callback no longer needs the registry lock.
Lworker_registry_reap_terminal:
    stp x19, x30, [sp, #-16]!
    mov x19, x0
    bl Lworker_registry_lock
    adrp x9, :got:vpp_worker_registry_closing
    ldr x9, [x9, :got_lo12:vpp_worker_registry_closing]
    ldr w9, [x9]
    cbnz w9, 1f
    mov x0, x19
    bl vpp_native_worker_join
1:  bl Lworker_registry_unlock
    ldp x19, x30, [sp], #16
    ret

// Root installs the native callback before the first spawn. No guest
// integer is interpreted as an executable address or as a record pointer.
.bss
.p2align 3
vpp_worker_service_entry: .skip 8
.text
.text
.globl vpp_native_worker_set_entry
vpp_native_worker_set_entry:
    stp x19, x30, [sp, #-16]!
    mov x19, x0
    bl vpp_native_worker_current
    cbnz x0, 1f
    bl Lworker_registry_lock
    adrp x9, :got:vpp_worker_registry_count
    ldr x9, [x9, :got_lo12:vpp_worker_registry_count]
    ldr w9, [x9]
    cbnz w9, 2f
    adrp x9, :got:vpp_worker_service_entry
    ldr x9, [x9, :got_lo12:vpp_worker_service_entry]
    str x19, [x9]
    bl Lworker_registry_unlock
    mov w0, #0
    b 3f
2:  bl Lworker_registry_unlock
1:  mov w0, #-1
3:  ldp x19, x30, [sp], #16
    ret

// Trusted machine ABI (op,a,b), exclusively i32 values:
// 0 available; 1 spawn(snapshot offset,length); 2 status(handle);
// 3 cancel(handle); 4 self; 5 current cancellation; 6 wait_begin(target);
// 7 wait_end; 8 callback result(handle); 9 copy_result(handle, descriptor);
// 10 shutdown; 11 open; 12 register fixed standalone VM callback (VM-only). Descriptor contains source,length,destination.
// Callback receives packet at 89000000 (maximum 31000000 bytes). Its VM
// must reserve that staging region until decoding completes.
// Callback result: nonnegative success, negative failure. Cancellation wins
// independently of that result and remains running until actual completion.
.globl vpp_native_worker_service
vpp_native_worker_service:
    stp x19, x20, [sp, #-48]!
    stp x21, x30, [sp, #16]
    mov w19, w0
    mov w20, w1
    mov w21, w2
#ifdef VPP_STANDALONE_VM_WORKER
    cmp w19, #12
    b.eq Lworker_service_vm_register
#endif
    cmp w19, #11
    b.hi Lworker_service_invalid
    cbz w19, Lworker_service_available
    cmp w19, #1
    b.eq Lworker_service_spawn
    cmp w19, #4
    b.lt Lworker_service_lookup
    cmp w19, #7
    b.le Lworker_service_self
    cmp w19, #9
    b.le Lworker_service_output
    cmp w19, #11
    b.le Lworker_service_root
    b Lworker_service_invalid
Lworker_service_available:
    adrp x9, :got:vpp_worker_service_entry
    ldr x9, [x9, :got_lo12:vpp_worker_service_entry]
    ldr x9, [x9]
    cmp x9, #0
    cset w0, ne
    b Lworker_service_return
Lworker_service_spawn:
    mov w9, #0x05c0
    movk w9, #0x1d9, lsl #16 // 31000000
    cmp w21, w9
    b.hi Lworker_service_invalid
    adrp x9, :got:vpp_worker_service_entry
    ldr x9, [x9, :got_lo12:vpp_worker_service_entry]
    ldr x0, [x9]
    cbz x0, Lworker_service_invalid
    mov w1, w20
    mov w2, w21
    mov w3, #0x0840
    movk w3, #0x54e, lsl #16 // 89000000
    bl vpp_native_worker_registry_start_copy
    b Lworker_service_return
Lworker_service_lookup:
    mov w0, w20
    bl vpp_native_worker_registry_record
    cbz x0, Lworker_service_invalid
    cmp w19, #3
    b.eq Lworker_service_cancel
    mov x20, x0
    bl vpp_native_worker_state
    cbz w0, Lworker_service_return
    mov w21, w0
    mov x0, x20
    bl Lworker_registry_reap_terminal
    mov x0, x20
    bl vpp_native_worker_cancelled
    cbnz w0, Lworker_service_cancelled
    cmp w21, #1
    b.ne Lworker_service_failed
    ldr w9, [x20, #40]
    tbnz w9, #31, Lworker_service_failed
    mov w0, #1
    b Lworker_service_return
Lworker_service_cancel:
    bl vpp_native_worker_request_cancel
    b Lworker_service_return
Lworker_service_cancelled:
    mov w0, #2
    b Lworker_service_return
Lworker_service_failed:
    mov w0, #3
    b Lworker_service_return
Lworker_service_self:
    bl vpp_native_worker_current
    cbz x0, Lworker_service_self_root
    cmp w19, #5
    b.eq Lworker_service_current_cancel
    bl vpp_native_worker_registry_self
    b Lworker_service_self_handle
Lworker_service_current_cancel:
    bl vpp_native_worker_cancelled
    b Lworker_service_return
Lworker_service_self_root:
    mov w0, #0
Lworker_service_self_handle:
    cmp w19, #6
    b.eq Lworker_service_wait_begin
    cmp w19, #7
    b.eq Lworker_service_wait_end
    b Lworker_service_return
Lworker_service_wait_begin:
    mov w1, w20
    bl vpp_native_worker_registry_wait_begin
    b Lworker_service_return
Lworker_service_wait_end:
    bl vpp_native_worker_registry_wait_end
    mov w0, #0
    b Lworker_service_return
Lworker_service_output:
    cmp w19, #9
    b.eq Lworker_service_copy
    mov w0, w20
    bl vpp_native_worker_registry_record
    cbz x0, Lworker_service_invalid
    add x1, sp, #32
    bl vpp_native_worker_result
    cbnz w0, Lworker_service_invalid
    ldr w0, [sp, #32]
    b Lworker_service_return
Lworker_service_copy:
    mov w9, #0x13f4
    movk w9, #0x810, lsl #16
    cmp w21, w9
    b.hi Lworker_service_invalid
    bl vpp_native_memory_base
    cbz x0, Lworker_service_invalid
    add x9, x0, w21, uxtw
    ldp w1, w2, [x9]
    ldr w3, [x9, #8]
    mov w0, w20
    bl vpp_native_worker_registry_copy_result
    b Lworker_service_return
Lworker_service_root:
    bl vpp_native_worker_current
    cbnz x0, Lworker_service_invalid
    cmp w19, #11
    b.eq Lworker_service_open
    cmp w19, #10
    b.ne Lworker_service_invalid
    bl vpp_native_worker_registry_shutdown
    b Lworker_service_return
Lworker_service_open:
    bl vpp_native_worker_registry_open
    b Lworker_service_return
#ifdef VPP_STANDALONE_VM_WORKER
Lworker_service_vm_register:
    bl vpp_native_worker_current
    cbnz x0, Lworker_service_invalid
    bl vpp_native_worker_registry_open
    cbnz w0, Lworker_service_return
    adrp x0, :got:vpp_worker_vm_entry_callback
    ldr x0, [x0, :got_lo12:vpp_worker_vm_entry_callback]
    // GOT address already resolved in x0
    bl vpp_native_worker_set_entry
    b Lworker_service_return
vpp_worker_vm_entry_callback:
    sub sp, sp, #48
    str x30, [sp, #32]
    str w1, [sp]
    str x0, [sp, #40]
    bl vpp_fn_vm_worker_entry
    mov w1, w0
    ldr x0, [sp, #40]
    ldr x30, [sp, #32]
    add sp, sp, #48
    b vpp_native_worker_compact_result
#endif
Lworker_service_invalid:
    mov w0, #-1
Lworker_service_return:
    ldp x21, x30, [sp, #16]
    ldp x19, x20, [sp], #48
    ret
