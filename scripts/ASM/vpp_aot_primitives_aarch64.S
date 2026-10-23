// Small machine/OS boundary for runtime.vi on macOS ARM64.
// No buffer, handle, file-lifecycle or number-conversion algorithms live here.
// The arena is mmap-backed (64 slots of 1 MiB plus metadata and a private linker argv region).
#ifndef VPP_NATIVE_RUNTIME_STACK_ABI
#define VPP_NATIVE_RUNTIME_STACK_ABI 0
#endif

.text
.p2align 2
.globl _vpp_native_memory_base
_vpp_native_memory_base:
    adrp x9, _vpp_native_arena_base@PAGE
    ldr x0, [x9, _vpp_native_arena_base@PAGEOFF]
    cbnz x0, 1f
    str x30, [sp, #-16]!
    mov x0, #0
    mov x1, #0x1400
    movk x1, #0x410, lsl #16  // 68,162,560 bytes
    mov x2, #3               // PROT_READ | PROT_WRITE, never executable
    mov x3, #0x1002          // MAP_PRIVATE | MAP_ANON
    mov x4, #-1
    mov x5, #0
    bl _mmap
    cmn x0, #1
    b.eq 2f
    adrp x9, _vpp_native_arena_base@PAGE
    str x0, [x9, _vpp_native_arena_base@PAGEOFF]
    ldr x30, [sp], #16
1:  ret
2:  mov x0, #0
    ldr x30, [sp], #16
    ret

.macro MEMORY_ENTER
    stp x0, x1, [sp, #-32]!
    str x30, [sp, #16]
    bl _vpp_native_memory_base
    mov x9, x0
    ldp x0, x1, [sp]
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

.globl _vpp_native_memory_read_byte
_vpp_native_memory_read_byte:
    MEMORY_ENTER
    MEMORY_BOUNDS 1
    ldrb w0, [x9, w0, uxtw]
    MEMORY_RETURN
.globl _vpp_native_memory_write_byte
_vpp_native_memory_write_byte:
    MEMORY_ENTER
    MEMORY_BOUNDS 1
    cmp w1, #255
    b.hi Lmemory_error
    strb w1, [x9, w0, uxtw]
    mov w0, #0
    MEMORY_RETURN
.globl _vpp_native_memory_read_i32
_vpp_native_memory_read_i32:
    MEMORY_ENTER
    MEMORY_BOUNDS 4
    ldr w0, [x9, w0, uxtw]
    MEMORY_RETURN
.globl _vpp_native_memory_write_i32
_vpp_native_memory_write_i32:
    MEMORY_ENTER
    MEMORY_BOUNDS 4
    str w1, [x9, w0, uxtw]
    mov w0, #0
    MEMORY_RETURN
.globl _vpp_native_memory_bind
_vpp_native_memory_bind:
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
    str x1, [x9, w0, uxtw]  // Store all 64 pointer bits; never an i32 cast.
    mov w0, #0
    MEMORY_RETURN
Lmemory_error:
    mov w0, #-1
    MEMORY_RETURN

.macro IO_FUNCTION name, target
    .globl _\name
_\name:
    stp x0, x1, [sp, #-32]!
    stp x30, x2, [sp, #16]
    bl _vpp_native_memory_base
    mov x9, x0
    ldp x0, x1, [sp]
    ldr x2, [sp, #24]
    cbz x9, Lio_error
    cmp w1, #0
    b.lt Lio_error
    cmp w2, #0
    b.lt Lio_error
    uxtw x1, w1
    uxtw x2, w2
    add x10, x1, x2
    mov x11, #0x1400
    movk x11, #0x410, lsl #16
    cmp x10, x11
    b.hi Lio_error
    add x1, x9, x1
    bl _\target
    MEMORY_RETURN
.endm
IO_FUNCTION vpp_native_os_read, read
IO_FUNCTION vpp_native_os_write, write
Lio_error:
    mov w0, #-1
    MEMORY_RETURN

.globl _vpp_native_os_open
_vpp_native_os_open:
    MEMORY_ENTER
    MEMORY_BOUNDS 1
    add x0, x9, w0, uxtw
    // Darwin's variadic arguments are passed on the stack, including mode.
    mov x10, #438            // 0666, subject to process umask
    str x10, [sp]
    bl _open
    MEMORY_RETURN
.globl _vpp_native_os_close
_vpp_native_os_close:
    b _close
.globl _vpp_native_os_errno
_vpp_native_os_errno:
    str x30, [sp, #-16]!
    bl ___error
    ldr w0, [x0]
    ldr x30, [sp], #16
    ret

// The V++ runtime validates both handles and their NUL-terminated paths.
// Only the machine boundary turns arena offsets into full 64-bit OS pointers.
// mkstemp modifies the trailing XXXXXX in the same arena bytes handle.
.globl _vpp_native_os_mkstemp
_vpp_native_os_mkstemp:
    MEMORY_ENTER
    MEMORY_BOUNDS 4096
    add x0, x9, w0, uxtw
    bl _mkstemp
    MEMORY_RETURN

.globl _vpp_native_os_rename
_vpp_native_os_rename:
    stp x19, x20, [sp, #-32]!
    stp x21, x30, [sp, #16]
    mov w19, w0
    mov w20, w1
    bl _vpp_native_memory_base
    cbz x0, Lrename_error
    cmp w19, #0
    b.lt Lrename_error
    cmp w20, #0
    b.lt Lrename_error
    mov w9, #0x400
    movk w9, #0x410, lsl #16 // arena size minus 4096
    cmp w19, w9
    b.hi Lrename_error
    cmp w20, w9
    b.hi Lrename_error
    add x1, x0, w20, uxtw
    add x0, x0, w19, uxtw
    bl _rename
    b Lrename_return
Lrename_error:
    mov w0, #-1
Lrename_return:
    ldp x21, x30, [sp, #16]
    ldp x19, x20, [sp], #32
    ret

// The V++ linker writes full-width argv pointers in its private arena.
// Only the machine boundary passes these pointers into posix_spawn.
// Input must be the dedicated 4096-entry table, after the payload slots.
.globl _vpp_native_os_spawn_clang
_vpp_native_os_spawn_clang:
    MEMORY_ENTER
    mov w10, #0x1400
    movk w10, #0x400, lsl #16
    cmp w0, w10
    b.ne Lspawn_error
    add x10, x9, w0, uxtw
    ldr x11, [x10]
    cbz x11, Lspawn_error
    // Preserve argv across _NSGetEnviron; the C ABI may overwrite x4.
    sub sp, sp, #32
    str x10, [sp]
    bl __NSGetEnviron
    cbz x0, Lspawn_failed
    ldr x5, [x0]
    cbz x5, Lspawn_failed
    ldr x4, [sp]
    add x0, sp, #16
    adrp x1, Lclang_path@PAGE
    add x1, x1, Lclang_path@PAGEOFF
    mov x2, #0
    mov x3, #0
    bl _posix_spawn
    cbnz w0, Lspawn_failed
    ldr w0, [sp, #16]
    add sp, sp, #32
    MEMORY_RETURN
Lspawn_failed:
    add sp, sp, #32
Lspawn_error:
    mov w0, #-1
    MEMORY_RETURN

.globl _vpp_native_os_waitpid
_vpp_native_os_waitpid:
    MEMORY_ENTER
    cmp w0, #1
    b.lt Lwait_error
    cmp w1, #0
    b.lt Lwait_error
    mov w10, #(0x1400 - 4)
    movk w10, #0x410, lsl #16
    cmp w1, w10
    b.hi Lwait_error
    add x1, x9, w1, uxtw
    mov w2, #0
    bl _waitpid
    MEMORY_RETURN
Lwait_error:
    mov w0, #-1
    MEMORY_RETURN

// Darwin arm64 struct stat has st_dev at +0, st_mode at +4 and the full
// 64-bit st_ino at +8. Two disjoint 208-byte arena scratch windows are
// reserved for comparing source and destination file identities in V++.
// Restrict the destination to those windows; path offsets are checked with
// enough room for the NUL terminator and a full PATH_MAX-sized path.
.macro LINK_STAT name, target
    .globl _\name
_\name:
    MEMORY_ENTER
    MEMORY_BOUNDS 4096
    cmp w1, #3376
    b.eq 1f
    cmp w1, #3584
    b.ne Llink_stat_error
1:  add x1, x9, w1, uxtw
    add x0, x9, w0, uxtw
    bl _\target
    MEMORY_RETURN
.endm
LINK_STAT vpp_native_os_stat, stat
LINK_STAT vpp_native_os_lstat, lstat
Llink_stat_error:
    mov w0, #-1
    MEMORY_RETURN

.globl _vpp_native_os_access_exec
_vpp_native_os_access_exec:
    MEMORY_ENTER
    MEMORY_BOUNDS 4096
    add x0, x9, w0, uxtw
    mov w1, #1                 // X_OK; policy stays in V++.
    bl _access
    MEMORY_RETURN

// Write an OS path into an arena byte slot (4096-byte maximum, NUL included).
// The caller owns the handle and records its length in V++; no pointers are
// narrowed to i32 or stored outside the 64-bit runtime arena.
.macro PATH_ENTER
    stp x19, x20, [sp, #-32]!
    stp x21, x30, [sp, #16]
    mov w19, w0
    bl _vpp_native_memory_base
    cbz x0, Lpath_error
    cmp w19, #0
    b.lt Lpath_error
    mov w9, #0x400
    movk w9, #0x410, lsl #16 // arena size minus 4096
    cmp w19, w9
    b.hi Lpath_error
    add x20, x0, w19, uxtw
.endm
.macro PATH_RETURN
    ldp x21, x30, [sp, #16]
    ldp x19, x20, [sp], #32
    ret
.endm
// fstat accepts a descriptor and one of the two reserved struct stat areas.
.globl _vpp_native_os_fstat
_vpp_native_os_fstat:
    MEMORY_ENTER
    cmp w1, #3376
    b.eq Lfstat_valid
    cmp w1, #3584
    b.ne Lfstat_error
Lfstat_valid:
    add x1, x9, w1, uxtw
    bl _fstat
    MEMORY_RETURN
Lfstat_error:
    mov w0, #-1
    MEMORY_RETURN

// unlink, mkdir and getenv accept arena offsets, never narrowed pointers.
.globl _vpp_native_os_unlink
_vpp_native_os_unlink:
    PATH_ENTER
    mov x0, x20
    bl _unlink
    PATH_RETURN

.globl _vpp_native_os_mkdir
_vpp_native_os_mkdir:
    PATH_ENTER
    mov x0, x20
    mov w1, #448            // mode 0700
    bl _mkdir
    PATH_RETURN

.globl _vpp_native_os_env_copy
_vpp_native_os_env_copy:
    MEMORY_ENTER
    MEMORY_BOUNDS 4096
    mov w12, w0
    mov w0, w1
    MEMORY_BOUNDS 4096
    add x10, x9, w1, uxtw
    str x10, [sp, #24]       // unused MEMORY_ENTER frame slot
    add x0, x9, w12, uxtw
    bl _getenv
    cbz x0, Lenv_error
    ldr x11, [sp, #24]
    mov w9, #0
Lenv_copy:
    ldrb w10, [x0, x9]
    cbz w10, Lenv_done
    cmp w9, #4095
    b.hs Lenv_error
    strb w10, [x11, x9]
    add w9, w9, #1
    b Lenv_copy
Lenv_done:
    strb wzr, [x11, x9]
    mov w0, w9
    MEMORY_RETURN
Lenv_error:
    mov w0, #-1
    MEMORY_RETURN

// Streaming SHA-256 uses 104-byte CC_SHA256_CTX in the metadata page.
.globl _vpp_native_sha_begin
_vpp_native_sha_begin:
    MEMORY_ENTER
    add x0, x9, #3904
    bl _CC_SHA256_Init
    cmp w0, #1
    cset w0, ne
    neg w0, w0
    MEMORY_RETURN

.globl _vpp_native_sha_update
_vpp_native_sha_update:
    MEMORY_ENTER
    cmp w0, #0
    b.lt Lsha_error
    cmp w1, #0
    b.lt Lsha_error
    uxtw x10, w0
    uxtw x11, w1
    add x10, x10, x11
    mov x11, #0x1400
    movk x11, #0x410, lsl #16
    cmp x10, x11
    b.hi Lsha_error
    mov w2, w1
    add x1, x9, w0, uxtw
    add x0, x9, #3904
    bl _CC_SHA256_Update
    cmp w0, #1
    cset w0, ne
    neg w0, w0
    MEMORY_RETURN

.globl _vpp_native_sha_end
_vpp_native_sha_end:
    MEMORY_ENTER
    MEMORY_BOUNDS 32
    add x1, x9, #3904
    add x0, x9, w0, uxtw
    bl _CC_SHA256_Final
    cmp w0, #1
    cset w0, ne
    neg w0, w0
    MEMORY_RETURN
Lsha_error:
    mov w0, #-1
    MEMORY_RETURN

// atexit invokes the exported V++ callback via the normal C ABI adapter.
.globl _vpp_native_os_register_cache_report
_vpp_native_os_register_cache_report:
    str x30, [sp, #-16]!
    adrp x0, _vpp_native_cache_report_entry@PAGE
    add x0, x0, _vpp_native_cache_report_entry@PAGEOFF
    bl _atexit
    ldr x30, [sp], #16
    ret

.globl _vpp_native_os_executable_path
_vpp_native_os_executable_path:
    PATH_ENTER
    sub sp, sp, #16
    mov w9, #4096
    str w9, [sp]
    mov x0, x20
    mov x1, sp
    bl __NSGetExecutablePath
    cbnz w0, Lexe_error
    mov w9, #0
Lexe_length:
    ldrb w10, [x20, x9]
    cbz w10, Lexe_done
    add w9, w9, #1
    cmp w9, #4095
    b.hi Lexe_error
    b Lexe_length
Lexe_done:
    mov w0, w9
    add sp, sp, #16
    PATH_RETURN
Lexe_error:
    add sp, sp, #16
    b Lpath_error

.globl _vpp_native_os_home
_vpp_native_os_home:
    PATH_ENTER
    adrp x0, Lhome_key@PAGE
    add x0, x0, Lhome_key@PAGEOFF
    bl _getenv
    cbz x0, Lpath_error
    mov x21, x0
    mov w9, #0
Lhome_copy:
    ldrb w10, [x21, x9]
    cbz w10, Lhome_done
    cmp w9, #4095
    b.hs Lpath_error
    strb w10, [x20, x9]
    add x9, x9, #1
    b Lhome_copy
Lhome_done:
    strb wzr, [x20, x9]
    mov w0, w9
    PATH_RETURN
.globl _vpp_native_os_cwd
_vpp_native_os_cwd:
    PATH_ENTER
    mov x0, x20
    mov x1, #4096
    bl _getcwd
    cbz x0, Lpath_error
    mov w9, #0
Lcwd_length:
    ldrb w10, [x20, x9]
    cbz w10, Lcwd_done
    add x9, x9, #1
    cmp w9, #4096
    b.hs Lpath_error
    b Lcwd_length
Lcwd_done:
    mov w0, w9
    PATH_RETURN
Lpath_error:
    mov w0, #-1
    PATH_RETURN

.section __TEXT,__cstring,cstring_literals
Lhome_key:
    .asciz "VPP_HOME"
Lclang_path:
    .asciz "/usr/bin/clang"
.text
.globl _vpp_native_i32_div
_vpp_native_i32_div:
    cbz w1, 1f
    sdiv w0, w0, w1
    ret
1:  mov w0, #-1
    ret

.globl _vpp_native_argc
_vpp_native_argc:
    str x30, [sp, #-16]!
    bl __NSGetArgc
    ldr w0, [x0]
    ldr x30, [sp], #16
    ret

// Raw argv pointers remain 64 bit inside this machine boundary.
Largument_pointer:
    stp x0, x30, [sp, #-16]!
    bl __NSGetArgc
    ldr w9, [x0]
    ldr w10, [sp]
    cmp w10, #0
    b.lt Largument_invalid
    cmp w10, w9
    b.ge Largument_invalid
    bl __NSGetArgv
    ldr x0, [x0]
    ldr w10, [sp]
    ldr x0, [x0, w10, uxtw #3]
    ldr x30, [sp, #8]
    add sp, sp, #16
    ret
Largument_invalid:
    mov x0, #0
    ldr x30, [sp, #8]
    add sp, sp, #16
    ret
.globl _vpp_native_os_argument_length
_vpp_native_os_argument_length:
    str x30, [sp, #-16]!
    bl Largument_pointer
    cbz x0, Largument_length_error
    mov x9, #0
    mov x11, #0x100000
Largument_length_loop:
    cmp x9, x11
    b.hi Largument_length_error
    ldrb w10, [x0, x9]
    cbz w10, Largument_length_done
    add x9, x9, #1
    b Largument_length_loop
Largument_length_done:
    mov w0, w9
    ldr x30, [sp], #16
    ret
Largument_length_error:
    mov w0, #-1
    ldr x30, [sp], #16
    ret
.globl _vpp_native_os_argument_byte
_vpp_native_os_argument_byte:
    stp x1, x30, [sp, #-16]!
    bl Largument_pointer
    cbz x0, Largument_byte_error
    ldr w1, [sp]
    cmp w1, #0
    b.lt Largument_byte_error
    mov w11, #0x100000
    cmp w1, w11
    b.hs Largument_byte_error
    mov x9, #0
Largument_byte_loop:
    ldrb w10, [x0, x9]
    cbz w10, Largument_byte_error
    cmp w9, w1
    b.eq Largument_byte_done
    add x9, x9, #1
    b Largument_byte_loop
Largument_byte_done:
    mov w0, w10
    ldr x30, [sp, #8]
    add sp, sp, #16
    ret
Largument_byte_error:
    mov w0, #-1
    ldr x30, [sp, #8]
    add sp, sp, #16
    ret

// Public runtime ABI adapters: Stage 0 uses AAPCS64; the native self-hosted
// compiler uses packed i32 stack arguments. Algorithms are the same .vi code.
.macro RUNTIME_EXPORT public, target, arity
    .globl _\public
_\public:
#if VPP_NATIVE_RUNTIME_STACK_ABI
    sub sp, sp, #32
    str x30, [sp, #16]
    .if \arity > 0
    str w0, [sp]
    .endif
    .if \arity > 1
    str w1, [sp, #4]
    .endif
    .if \arity > 2
    str w2, [sp, #8]
    .endif
    bl _vpp_fn_\target
    ldr x30, [sp, #16]
    add sp, sp, #32
    ret
#else
    b _\target
#endif
.endm
RUNTIME_EXPORT vpp_native_bytes_new, rt_bytes_new, 0
RUNTIME_EXPORT vpp_native_bytes_length, rt_bytes_length, 1
RUNTIME_EXPORT vpp_native_bytes_get, rt_bytes_get, 2
RUNTIME_EXPORT vpp_native_bytes_append, rt_bytes_append, 2
RUNTIME_EXPORT vpp_native_bytes_concat, rt_bytes_concat, 2
RUNTIME_EXPORT vpp_native_bytes_equal, rt_bytes_equal, 2
RUNTIME_EXPORT vpp_native_bytes_slice, rt_bytes_slice, 3
RUNTIME_EXPORT vpp_native_bytes_find, rt_bytes_find, 2
RUNTIME_EXPORT vpp_native_bytes_clear, rt_bytes_clear, 1
RUNTIME_EXPORT vpp_native_bytes_print, rt_bytes_print, 1
RUNTIME_EXPORT vpp_native_bytes_push_i32, rt_bytes_push_i32, 2
RUNTIME_EXPORT vpp_native_bytes_load_i32, rt_bytes_load_i32, 2
RUNTIME_EXPORT vpp_native_bytes_store_i32, rt_bytes_store_i32, 3
RUNTIME_EXPORT vpp_native_bytes_free, rt_bytes_free, 1
RUNTIME_EXPORT vpp_native_bytes_reserve_extra, rt_bytes_reserve, 2
RUNTIME_EXPORT vpp_native_argv, rt_argv, 1
RUNTIME_EXPORT vpp_native_file_open_read, rt_file_open_read, 1
RUNTIME_EXPORT vpp_native_file_open_write, rt_file_open_write, 1
RUNTIME_EXPORT vpp_native_file_read_block, rt_file_read_block, 2
RUNTIME_EXPORT vpp_native_file_write_block, rt_file_write_block, 2
RUNTIME_EXPORT vpp_native_file_read_byte, rt_file_read_byte, 1
RUNTIME_EXPORT vpp_native_file_write_byte, rt_file_write_byte, 2
RUNTIME_EXPORT vpp_native_file_close, rt_file_close, 1
RUNTIME_EXPORT vpp_native_print_i32, rt_print_i32, 1
RUNTIME_EXPORT vpp_native_read_i32, rt_read_i32, 0
RUNTIME_EXPORT vpp_native_read_status, rt_read_status, 0
RUNTIME_EXPORT vpp_native_runtime_module_path, rt_runtime_module_path, 0
RUNTIME_EXPORT vpp_native_runtime_file_path, rt_runtime_file_path, 1
RUNTIME_EXPORT vpp_native_object_services, rt_object_services, 3
RUNTIME_EXPORT vpp_native_object_archive_entry, rt_object_archive_entry, 2
RUNTIME_EXPORT vpp_native_object_archive_summary, rt_object_archive_summary, 1
RUNTIME_EXPORT vpp_native_link_archive_single, rt_link_archive_single, 1
RUNTIME_EXPORT vpp_native_link_validate_paths, rt_link_validate_paths, 2
RUNTIME_EXPORT vpp_native_link_temporary_path, rt_link_temporary_path, 3
RUNTIME_EXPORT vpp_native_link_temp_open, rt_link_temp_open, 1
RUNTIME_EXPORT vpp_native_link_publish, rt_link_publish, 2
RUNTIME_EXPORT vpp_native_link_spawn_wait, rt_link_spawn_wait, 0
RUNTIME_EXPORT vpp_native_link_validate_files, rt_link_validate_files, 2
RUNTIME_EXPORT vpp_native_link_executable_ready, rt_link_executable_ready, 1
RUNTIME_EXPORT vpp_native_link_write_object, rt_link_write_object, 3
RUNTIME_EXPORT vpp_native_cache_header_write, rt_cache_header_write, 2
RUNTIME_EXPORT vpp_native_cache_header_check, rt_cache_header_check, 2
RUNTIME_EXPORT vpp_native_cache_digest_hex, rt_cache_digest_hex, 0
RUNTIME_EXPORT vpp_native_cache_read, rt_cache_read, 2
RUNTIME_EXPORT vpp_native_cache_write, rt_cache_write, 2
RUNTIME_EXPORT vpp_native_link_macho, rt_link_macho, 3
RUNTIME_EXPORT vpp_native_link_macho_modules, rt_link_macho_modules, 3
RUNTIME_EXPORT vpp_native_cache_report_entry, rt_cache_report, 0

.zerofill __DATA,__bss,_vpp_native_arena_base,8,3
.subsections_via_symbols

#include "vpp_vietnamese_runtime_symbols.inc"
