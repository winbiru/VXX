// Linux AArch64, ELF, AAPCS64 + glibc and OpenSSL libcrypto.
// Independent OS boundary for runtime.vi; all arena pointers stay 64 bits.
// Assemble: clang -target aarch64-unknown-linux-gnu -c this-file.S
#ifndef VPP_NATIVE_RUNTIME_STACK_ABI
#define VPP_NATIVE_RUNTIME_STACK_ABI 0
#endif
#define PUBLIC(x) x
#if VPP_NATIVE_RUNTIME_STACK_ABI
#define RT_TARGET(x) PUBLIC(vpp_fn_##x)
#else
#define RT_TARGET(x) PUBLIC(x)
#endif

.text
.p2align 2
.globl vpp_native_memory_base
vpp_native_memory_base:
    adrp x9, :got:vpp_native_arena_base
    ldr x9, [x9, :got_lo12:vpp_native_arena_base]
    ldr x0, [x9]
    cbnz x0, 1f
    str x30, [sp, #-16]!
    mov x0, #0
    mov x1, #0x1400
    movk x1, #0x410, lsl #16  // 68,162,560 bytes
    mov x2, #3               // PROT_READ | PROT_WRITE
    mov x3, #0x22            // Linux MAP_PRIVATE | MAP_ANONYMOUS
    mov x4, #-1
    mov x5, #0
    bl mmap
    cmn x0, #1
    b.eq 2f
    adrp x9, :got:vpp_native_arena_base
    ldr x9, [x9, :got_lo12:vpp_native_arena_base]
    str x0, [x9]
    ldr x30, [sp], #16
1:  ret
2:  mov x0, #0
    ldr x30, [sp], #16
    ret

.macro MEMORY_ENTER
    stp x0, x1, [sp, #-32]!
    stp x30, x2, [sp, #16]
    bl vpp_native_memory_base
    mov x9, x0
    ldp x0, x1, [sp]
    ldr x2, [sp, #24]
    cbz x9, Lmemory_error
.endm
.macro MEMORY_BOUNDS width
    cmp w0, #0
    b.lt Lmemory_error
    mov w10, #(0x1400 - \width)
    movk w10, #0x410, lsl #16
    cmp w0, w10
    b.hi Lmemory_error
.endm
.macro MEMORY_RETURN
    ldr x30, [sp, #16]
    add sp, sp, #32
    ret
.endm
.globl vpp_native_memory_read_byte
vpp_native_memory_read_byte:
    MEMORY_ENTER
    MEMORY_BOUNDS 1
    ldrb w0, [x9, w0, uxtw]
    MEMORY_RETURN
.globl vpp_native_memory_write_byte
vpp_native_memory_write_byte:
    MEMORY_ENTER
    MEMORY_BOUNDS 1
    cmp w1, #255
    b.hi Lmemory_error
    strb w1, [x9, w0, uxtw]
    mov w0, #0
    MEMORY_RETURN
.globl vpp_native_memory_read_i32
vpp_native_memory_read_i32:
    MEMORY_ENTER
    MEMORY_BOUNDS 4
    ldr w0, [x9, w0, uxtw]
    MEMORY_RETURN
.globl vpp_native_memory_write_i32
vpp_native_memory_write_i32:
    MEMORY_ENTER
    MEMORY_BOUNDS 4
    str w1, [x9, w0, uxtw]
    mov w0, #0
    MEMORY_RETURN
.globl vpp_native_memory_bind
vpp_native_memory_bind:
    MEMORY_ENTER
    MEMORY_BOUNDS 8
    tst w0, #7
    b.ne Lmemory_error
    cmp w1, #0
    b.lt Lmemory_error
    mov w10, #0x1400
    movk w10, #0x410, lsl #16
    cmp w1, w10
    b.hs Lmemory_error
    add x1, x9, w1, uxtw
    str x1, [x9, w0, uxtw]
    mov w0, #0
    MEMORY_RETURN
Lmemory_error:
    mov w0, #-1
    MEMORY_RETURN

.macro IO_FUNC name, target
    .globl \name
\name:
    MEMORY_ENTER
    cmp w1, #0
    b.lt Lmemory_error
    cmp w2, #0
    b.lt Lmemory_error
    uxtw x1, w1
    uxtw x2, w2
    add x10, x1, x2
    mov x11, #0x1400
    movk x11, #0x410, lsl #16
    cmp x10, x11
    b.hi Lmemory_error
    add x1, x9, x1
    bl \target
    MEMORY_RETURN
.endm
IO_FUNC vpp_native_os_read, read
IO_FUNC vpp_native_os_write, write

.globl vpp_native_os_open
vpp_native_os_open:
    MEMORY_ENTER
    MEMORY_BOUNDS 4096
    add x0, x9, w0, uxtw
    // runtime.vi uses Darwin O_WRONLY|O_CREAT|O_TRUNC = 1537;
    // translate to Linux O_WRONLY|O_CREAT|O_TRUNC = 577.
    mov w10, #1537
    cmp w1, w10
    b.ne 1f
    mov w1, #577
1:
    mov w2, #438               // Linux open(2) mode, 0666
    bl open
    MEMORY_RETURN
.globl vpp_native_os_close
vpp_native_os_close:
    b close
.globl vpp_native_os_errno
vpp_native_os_errno:
    str x30, [sp, #-16]!
    bl __errno_location
    ldr w0, [x0]
    ldr x30, [sp], #16
    ret
.globl vpp_native_os_mkstemp
vpp_native_os_mkstemp:
    MEMORY_ENTER
    MEMORY_BOUNDS 4096
    add x0, x9, w0, uxtw
    bl mkstemp
    MEMORY_RETURN
.globl vpp_native_os_rename
vpp_native_os_rename:
    MEMORY_ENTER
    MEMORY_BOUNDS 4096
    mov w12, w0
    mov w0, w1
    MEMORY_BOUNDS 4096
    add x1, x9, w1, uxtw
    add x0, x9, w12, uxtw
    bl rename
    MEMORY_RETURN

.globl vpp_native_os_spawn_clang
vpp_native_os_spawn_clang:
    MEMORY_ENTER
    mov w10, #0x1400
    movk w10, #0x400, lsl #16
    cmp w0, w10
    b.ne Lmemory_error
    add x4, x9, w0, uxtw
    ldr x10, [x4]
    cbz x10, Lmemory_error
    adrp x5, :got:environ
    ldr x5, [x5, :got_lo12:environ]
    ldr x5, [x5]
    cbz x5, Lmemory_error
    add x0, sp, #24           // pid_t out (sp[24..27] unused)
    adrp x1, Lclang_path
    add x1, x1, :lo12:Lclang_path
    mov x2, #0
    mov x3, #0
    bl posix_spawn
    cbnz w0, Lmemory_error
    ldr w0, [sp, #24]
    MEMORY_RETURN
.globl vpp_native_os_waitpid
vpp_native_os_waitpid:
    MEMORY_ENTER
    cmp w0, #1
    b.lt Lmemory_error
    mov w12, w0
    mov w0, w1
    MEMORY_BOUNDS 4
    mov w0, w12
    add x1, x9, w1, uxtw
    mov w2, #0
    bl waitpid
    MEMORY_RETURN

// Linux AArch64 glibc struct stat: dev+0, ino+8, mode+16.
// Normalize mode into Darwin-compatible +4 for the current runtime.vi.
.macro STAT_FUNC name, target, uses_path
    .globl \name
\name:
    MEMORY_ENTER
    cmp w1, #3376
    b.eq 1f
    cmp w1, #3584
    b.ne Lmemory_error
1:  add x11, x9, w1, uxtw
    str x11, [sp, #24]
    .if \uses_path
        MEMORY_BOUNDS 4096
        add x0, x9, w0, uxtw
    .endif
    mov x1, x11
    bl \target
    cbnz w0, 2f
    ldr x11, [sp, #24]
    ldr w10, [x11, #16]
    str w10, [x11, #4]
2:  MEMORY_RETURN
.endm
STAT_FUNC vpp_native_os_stat, stat, 1
STAT_FUNC vpp_native_os_lstat, lstat, 1
STAT_FUNC vpp_native_os_fstat, fstat, 0
.globl vpp_native_os_access_exec
vpp_native_os_access_exec:
    MEMORY_ENTER
    MEMORY_BOUNDS 4096
    add x0, x9, w0, uxtw
    mov w1, #1
    bl access
    MEMORY_RETURN
.globl vpp_native_os_unlink
vpp_native_os_unlink:
    MEMORY_ENTER
    MEMORY_BOUNDS 4096
    add x0, x9, w0, uxtw
    bl unlink
    MEMORY_RETURN
.globl vpp_native_os_mkdir
vpp_native_os_mkdir:
    MEMORY_ENTER
    MEMORY_BOUNDS 4096
    add x0, x9, w0, uxtw
    mov w1, #448
    bl mkdir
    MEMORY_RETURN
.globl vpp_native_os_env_copy
vpp_native_os_env_copy:
    MEMORY_ENTER
    MEMORY_BOUNDS 4096
    mov w12, w0
    mov w0, w1
    MEMORY_BOUNDS 4096
    add x10, x9, w1, uxtw
    str x10, [sp, #24]
    add x0, x9, w12, uxtw
    bl getenv
    cbz x0, Lmemory_error
    ldr x11, [sp, #24]
    mov w9, #0
1:  ldrb w10, [x0, x9]
    cbz w10, 2f
    cmp w9, #4095
    b.hs Lmemory_error
    strb w10, [x11, x9]
    add w9, w9, #1
    b 1b
2:  strb wzr, [x11, x9]
    mov w0, w9
    MEMORY_RETURN

.globl vpp_native_os_executable_path
vpp_native_os_executable_path:
    MEMORY_ENTER
    MEMORY_BOUNDS 4096
    add x1, x9, w0, uxtw
    str x1, [sp, #24]
    adrp x0, Lexe_path
    add x0, x0, :lo12:Lexe_path
    mov x2, #4095
    bl readlink
    cmp x0, #0
    b.lt Lmemory_error
    cmp x0, #4095
    b.hs Lmemory_error
    ldr x1, [sp, #24]
    strb wzr, [x1, x0]
    MEMORY_RETURN
.globl vpp_native_os_home
vpp_native_os_home:
    MEMORY_ENTER
    MEMORY_BOUNDS 4096
    add x11, x9, w0, uxtw
    str x11, [sp, #24]
    adrp x0, Lhome_key
    add x0, x0, :lo12:Lhome_key
    bl getenv
    cbz x0, Lmemory_error
    ldr x11, [sp, #24]
    mov w9, #0
1:  ldrb w10, [x0, x9]
    cbz w10, 2f
    cmp w9, #4095
    b.hs Lmemory_error
    strb w10, [x11, x9]
    add w9, w9, #1
    b 1b
2:  strb wzr, [x11, x9]
    mov w0, w9
    MEMORY_RETURN
.globl vpp_native_os_cwd
vpp_native_os_cwd:
    MEMORY_ENTER
    MEMORY_BOUNDS 4096
    add x0, x9, w0, uxtw
    str x0, [sp, #24]
    mov x1, #4096
    bl getcwd
    cbz x0, Lmemory_error
    ldr x11, [sp, #24]
    mov w9, #0
1:  cmp w9, #4096
    b.hs Lmemory_error
    ldrb w10, [x11, x9]
    cbz w10, 2f
    add w9, w9, #1
    b 1b
2:  mov w0, w9
    MEMORY_RETURN

// OpenSSL SHA256_CTX (112 bytes) fits at arena offset 3904.
.globl vpp_native_sha_begin
vpp_native_sha_begin:
    MEMORY_ENTER
    add x0, x9, #3904
    bl SHA256_Init
    cmp w0, #1
    cset w0, ne
    neg w0, w0
    MEMORY_RETURN
.globl vpp_native_sha_update
vpp_native_sha_update:
    MEMORY_ENTER
    cmp w0, #0
    b.lt Lmemory_error
    cmp w1, #0
    b.lt Lmemory_error
    uxtw x10, w0
    uxtw x11, w1
    add x10, x10, x11
    mov x11, #0x1400
    movk x11, #0x410, lsl #16
    cmp x10, x11
    b.hi Lmemory_error
    mov w2, w1
    add x1, x9, w0, uxtw
    add x0, x9, #3904
    bl SHA256_Update
    cmp w0, #1
    cset w0, ne
    neg w0, w0
    MEMORY_RETURN
.globl vpp_native_sha_end
vpp_native_sha_end:
    MEMORY_ENTER
    MEMORY_BOUNDS 32
    add x1, x9, #3904
    add x0, x9, w0, uxtw
    bl SHA256_Final
    cmp w0, #1
    cset w0, ne
    neg w0, w0
    MEMORY_RETURN
.globl vpp_native_os_register_cache_report
vpp_native_os_register_cache_report:
    str x30, [sp, #-16]!
    adrp x0, :got:vpp_native_cache_report_entry
    ldr x0, [x0, :got_lo12:vpp_native_cache_report_entry]
    bl atexit
    ldr x30, [sp], #16
    ret
.globl vpp_native_i32_div
vpp_native_i32_div:
    cbz w1, 1f
    sdiv w0, w0, w1
    ret
1:  mov w0, #-1
    ret

// glibc exposes a pointer to the initial argc/argv stack image.
.globl vpp_native_argc
vpp_native_argc:
    adrp x9, :got:__libc_stack_end
    ldr x9, [x9, :got_lo12:__libc_stack_end]
    ldr x9, [x9]
    ldr w0, [x9]
    ret
Largument_pointer:
    cmp w0, #0
    b.lt 1f
    adrp x9, :got:__libc_stack_end
    ldr x9, [x9, :got_lo12:__libc_stack_end]
    ldr x9, [x9]
    ldr w10, [x9]
    cmp w0, w10
    b.ge 1f
    add x9, x9, #8
    ldr x0, [x9, w0, uxtw #3]
    ret
1:  mov x0, #0
    ret
.globl vpp_native_os_argument_length
vpp_native_os_argument_length:
    str x30, [sp, #-16]!
    bl Largument_pointer
    cbz x0, 2f
    mov x9, #0
    mov x11, #0x100000
1:  cmp x9, x11
    b.hi 2f
    ldrb w10, [x0, x9]
    cbz w10, 3f
    add x9, x9, #1
    b 1b
3:  mov w0, w9
    ldr x30, [sp], #16
    ret
2:  mov w0, #-1
    ldr x30, [sp], #16
    ret
.globl vpp_native_os_argument_byte
vpp_native_os_argument_byte:
    stp x1, x30, [sp, #-16]!
    bl Largument_pointer
    cbz x0, 2f
    ldr w1, [sp]
    cmp w1, #0
    b.lt 2f
    mov w11, #0x100000
    cmp w1, w11
    b.hs 2f
    mov x9, #0
1:  ldrb w10, [x0, x9]
    cbz w10, 2f
    cmp w9, w1
    b.eq 3f
    add x9, x9, #1
    b 1b
3:  mov w0, w10
    ldp x1, x30, [sp], #16
    ret
2:  mov w0, #-1
    ldp x1, x30, [sp], #16
    ret

.macro RT_EXPORT public, target, argc
    .globl \public
\public:
#if VPP_NATIVE_RUNTIME_STACK_ABI
    sub sp, sp, #32
    str x30, [sp, #16]
    .if \argc > 0
    str w0, [sp]
    .endif
    .if \argc > 1
    str w1, [sp, #4]
    .endif
    .if \argc > 2
    str w2, [sp, #8]
    .endif
    bl \target
    ldr x30, [sp, #16]
    add sp, sp, #32
    ret
#else
    b \target
#endif
.endm
#include "vpp_aot_runtime_exports.inc"

.section .rodata
Lhome_key: .asciz "VPP_HOME"
Lexe_path: .asciz "/proc/self/exe"
Lclang_path: .asciz "/usr/bin/clang"
.bss
.p2align 3
vpp_native_arena_base: .skip 8
.section .note.GNU-stack,"",@progbits

#include "vpp_vietnamese_runtime_symbols.inc"
