// Shared POSIX x86_64 machine boundary for runtime.vi.
// Same arena slots and exposed entry points as vpp_aot_primitives_aarch64.S.
// Full-sized pointer values are stored in the arena; public handles stay i32.
#ifndef VPP_NATIVE_RUNTIME_STACK_ABI
#define VPP_NATIVE_RUNTIME_STACK_ABI 0
#endif
#ifdef __APPLE__
#define PUBLIC(x) _##x
#define LIB(x) _##x
#else
#define PUBLIC(x) x
#define LIB(x) x@PLT
#endif
#ifdef __APPLE__
#include "vpp_vietnamese_runtime_symbols.inc"
#define RT_TARGET(x) x
#elif VPP_NATIVE_RUNTIME_STACK_ABI
#define RT_TARGET(x) PUBLIC(vpp_fn_##x)
#else
#define RT_TARGET(x) PUBLIC(x)
#endif
#define VPP_ARENA_SIZE 68162560
#define VPP_PATH_MAX 4096

.text
.p2align 4
.globl PUBLIC(vpp_native_memory_base)
PUBLIC(vpp_native_memory_base):
    movq PUBLIC(vpp_native_arena_base)(%rip), %rax
    testq %rax, %rax
    jne 1f
    subq $8, %rsp                 // align SysV call boundary
    xorl %edi, %edi
    movl $VPP_ARENA_SIZE, %esi
    movl $3, %edx                // PROT_READ | PROT_WRITE
#ifdef __APPLE__
    movl $0x1002, %ecx          // MAP_PRIVATE | MAP_ANON (Darwin)
#else
    movl $0x22, %ecx            // MAP_PRIVATE | MAP_ANONYMOUS (Linux)
#endif
    movq $-1, %r8
    xorl %r9d, %r9d
    call LIB(mmap)
    addq $8, %rsp
    cmpq $-1, %rax
    je 2f
    movq %rax, PUBLIC(vpp_native_arena_base)(%rip)
1:  ret
2:  xorl %eax, %eax
    ret

// Frame preserves three 32-bit inputs across mmap initialization.
.macro MEM_ENTER
    pushq %rbp
    movq %rsp, %rbp
    subq $(80), %rsp
    movl %edi, -4(%rbp)
    movl %esi, -8(%rbp)
    movl %edx, -12(%rbp)
    call PUBLIC(vpp_native_memory_base)
    movq %rax, %r11
    movl -4(%rbp), %edi
    movl -8(%rbp), %esi
    movl -12(%rbp), %edx
    testq %r11, %r11
    jz Lmem_error
.endm
.macro MEM_LEAVE
    leave
    ret
.endm
.macro CHECK_OFFSET reg, width
    cmpl $(VPP_ARENA_SIZE - \width), \reg
    ja Lmem_error                 // unsigned comparison also rejects negatives
.endm
.macro CHECK_PATH reg
    CHECK_OFFSET \reg, VPP_PATH_MAX
.endm

.globl PUBLIC(vpp_native_memory_read_byte)
PUBLIC(vpp_native_memory_read_byte):
    MEM_ENTER
    CHECK_OFFSET %edi, 1
    movzbl (%r11,%rdi), %eax
    MEM_LEAVE
.globl PUBLIC(vpp_native_memory_write_byte)
PUBLIC(vpp_native_memory_write_byte):
    MEM_ENTER
    CHECK_OFFSET %edi, 1
    cmpl $255, %esi
    ja Lmem_error
    movb %sil, (%r11,%rdi)
    xorl %eax, %eax
    MEM_LEAVE
.globl PUBLIC(vpp_native_memory_read_i32)
PUBLIC(vpp_native_memory_read_i32):
    MEM_ENTER
    CHECK_OFFSET %edi, 4
    movl (%r11,%rdi), %eax
    MEM_LEAVE
.globl PUBLIC(vpp_native_memory_write_i32)
PUBLIC(vpp_native_memory_write_i32):
    MEM_ENTER
    CHECK_OFFSET %edi, 4
    movl %esi, (%r11,%rdi)
    xorl %eax, %eax
    MEM_LEAVE
.globl PUBLIC(vpp_native_memory_bind)
PUBLIC(vpp_native_memory_bind):
    MEM_ENTER
    CHECK_OFFSET %edi, 8
    testl $7, %edi
    jnz Lmem_error
    CHECK_OFFSET %esi, 1
    leaq (%r11,%rsi), %rax
    movq %rax, (%r11,%rdi)      // preserve full 64-bit address
    xorl %eax, %eax
    MEM_LEAVE
Lmem_error:
    movl $-1, %eax
    MEM_LEAVE

.macro IO_FUNC name, target
    .globl \name
\name:
    MEM_ENTER
    CHECK_OFFSET %esi, 0
    cmpl $(0), %edx
    jl Lmem_error
    movl %esi, %eax
    addq %rdx, %rax
    cmpq $(VPP_ARENA_SIZE), %rax
    ja Lmem_error
    leaq (%r11,%rsi), %rsi
    movslq %edx, %rdx
    call \target
    MEM_LEAVE
.endm
IO_FUNC PUBLIC(vpp_native_os_read), LIB(read)
IO_FUNC PUBLIC(vpp_native_os_write), LIB(write)

.globl PUBLIC(vpp_native_os_open)
PUBLIC(vpp_native_os_open):
    MEM_ENTER
    CHECK_PATH %edi
    leaq (%r11,%rdi), %rdi
#ifndef __APPLE__
    // runtime.vi passes Darwin O_WRONLY|O_CREAT|O_TRUNC = 0x601.
    // Linux expects O_WRONLY|O_CREAT|O_TRUNC = 0x241.
    cmpl $1537, %esi
    jne 1f
    movl $577, %esi
1:
#endif
    movl $438, %edx             // 0666, process umask applies
#ifdef __APPLE__
    movq %rdx, (%rsp)           // Darwin x86_64 variadic arguments use stack
#endif
    xorl %eax, %eax
    call LIB(open)
    MEM_LEAVE
.globl PUBLIC(vpp_native_os_close)
PUBLIC(vpp_native_os_close):
    jmp LIB(close)
.globl PUBLIC(vpp_native_os_errno)
PUBLIC(vpp_native_os_errno):
    subq $8, %rsp
#ifdef __APPLE__
    call LIB(__error)
#else
    call LIB(__errno_location)
#endif
    movl (%rax), %eax
    addq $8, %rsp
    ret

.globl PUBLIC(vpp_native_os_mkstemp)
PUBLIC(vpp_native_os_mkstemp):
    MEM_ENTER
    CHECK_PATH %edi
    leaq (%r11,%rdi), %rdi
    call LIB(mkstemp)
    MEM_LEAVE
.globl PUBLIC(vpp_native_os_rename)
PUBLIC(vpp_native_os_rename):
    MEM_ENTER
    CHECK_PATH %edi
    CHECK_PATH %esi
    leaq (%r11,%rdi), %rdi
    leaq (%r11,%rsi), %rsi
    call LIB(rename)
    MEM_LEAVE

// V++ supplies a 4096-entry argv pointer table at offset 0x04001400.
.globl PUBLIC(vpp_native_os_spawn_clang)
PUBLIC(vpp_native_os_spawn_clang):
    MEM_ENTER
    cmpl $0x04001400, %edi
    jne Lmem_error
    leaq (%r11,%rdi), %rax
    cmpq $0, (%rax)
    je Lmem_error
    movq %rax, -32(%rbp)       // argv survives getenv/environ lookup
#ifdef __APPLE__
    call LIB(_NSGetEnviron)
    movq (%rax), %r9
#else
    movq environ@GOTPCREL(%rip), %rax
    movq (%rax), %r9
#endif
    leaq -40(%rbp), %rdi       // pid_t *
#ifdef __APPLE__
    leaq Lclang_path(%rip), %rsi
#else
    movq -32(%rbp), %rsi
    movq (%rsi), %rsi          // V++ argv[0] is the linker executable
#endif
    xorl %edx, %edx           // file actions
    xorl %ecx, %ecx           // attributes
    movq -32(%rbp), %r8       // argv
#ifdef __APPLE__
    call LIB(posix_spawn)
#else
    call LIB(posix_spawnp)     // Locate cc in PATH; Ubuntu need not ship clang
#endif
    testl %eax, %eax
    jne Lspawn_fail
    movl -40(%rbp), %eax
    MEM_LEAVE
Lspawn_fail:
    movl $-1, %eax
    MEM_LEAVE
.globl PUBLIC(vpp_native_os_waitpid)
PUBLIC(vpp_native_os_waitpid):
    MEM_ENTER
    cmpl $1, %edi
    jl Lmem_error
    CHECK_OFFSET %esi, 4
    leaq (%r11,%rsi), %rsi
    xorl %edx, %edx
    call LIB(waitpid)
    MEM_LEAVE

.macro STAT_FUNC name, target, path
    .globl \name
\name:
    MEM_ENTER
    cmpl $(3376), %esi
    je 1f
    cmpl $(3584), %esi
    jne Lmem_error
1:
    leaq (%r11,%rsi), %rsi
    movq %rsi, -32(%rbp)
    .if \path
        CHECK_PATH %edi
        leaq (%r11,%rdi), %rdi
    .endif
    call \target
#ifndef __APPLE__
    // glibc x86_64 struct stat: dev +0, ino +8, mode +24.
    // The V++ runtime currently reads the Darwin-style dev +0/mode +4/ino +8.
    testl %eax, %eax
    jne 2f
    movq -32(%rbp), %r10
    movl 24(%r10), %r9d
    movl %r9d, 4(%r10)
2:
#endif
    MEM_LEAVE
.endm
#ifdef __APPLE__
// Darwin x86_64's plain _stat/_lstat/_fstat are legacy inode32 variants.
// The modern struct stat expected by runtime.vi requires $INODE64 symbols.
STAT_FUNC PUBLIC(vpp_native_os_stat), _stat$INODE64, 1
STAT_FUNC PUBLIC(vpp_native_os_lstat), _lstat$INODE64, 1
STAT_FUNC PUBLIC(vpp_native_os_fstat), _fstat$INODE64, 0
#else
STAT_FUNC PUBLIC(vpp_native_os_stat), LIB(stat), 1
STAT_FUNC PUBLIC(vpp_native_os_lstat), LIB(lstat), 1
STAT_FUNC PUBLIC(vpp_native_os_fstat), LIB(fstat), 0
#endif

.globl PUBLIC(vpp_native_os_access_exec)
PUBLIC(vpp_native_os_access_exec):
    MEM_ENTER
    CHECK_PATH %edi
    leaq (%r11,%rdi), %rdi
    movl $1, %esi
    call LIB(access)
    MEM_LEAVE
.globl PUBLIC(vpp_native_os_unlink)
PUBLIC(vpp_native_os_unlink):
    MEM_ENTER
    CHECK_PATH %edi
    leaq (%r11,%rdi), %rdi
    call LIB(unlink)
    MEM_LEAVE
.globl PUBLIC(vpp_native_os_mkdir)
PUBLIC(vpp_native_os_mkdir):
    MEM_ENTER
    CHECK_PATH %edi
    leaq (%r11,%rdi), %rdi
    movl $448, %esi           // mode 0700
    call LIB(mkdir)
    MEM_LEAVE

// Copy NUL-terminated string to a 4096-byte slot; return length or -1.
// rdi=destination, rsi=source; preserves no caller-saved register.
Lcopy_path:
    xorl %eax, %eax
1:  cmpl $4095, %eax
    jae 2f
    movb (%rsi,%rax), %cl
    movb %cl, (%rdi,%rax)
    testb %cl, %cl
    je 3f
    incl %eax
    jmp 1b
2:  movl $-1, %eax
3:  ret

.globl PUBLIC(vpp_native_os_env_copy)
PUBLIC(vpp_native_os_env_copy):
    MEM_ENTER
    CHECK_PATH %edi
    CHECK_PATH %esi
    leaq (%r11,%rsi), %rax
    movq %rax, -32(%rbp)
    leaq (%r11,%rdi), %rdi
    call LIB(getenv)
    testq %rax, %rax
    je Lmem_error
    movq %rax, %rsi
    movq -32(%rbp), %rdi
    call Lcopy_path
    MEM_LEAVE

.globl PUBLIC(vpp_native_os_executable_path)
PUBLIC(vpp_native_os_executable_path):
    MEM_ENTER
    CHECK_PATH %edi
    leaq (%r11,%rdi), %rdi
#ifdef __APPLE__
    leaq -40(%rbp), %rsi
    movl $4096, -40(%rbp)
    call LIB(_NSGetExecutablePath)
    testl %eax, %eax
    jne Lmem_error
    movq PUBLIC(vpp_native_arena_base)(%rip), %r11
    movl -4(%rbp), %edi
    leaq (%r11,%rdi), %rdi
    movq %rdi, %rsi
    // Scan in-place without copying.
    xorl %eax, %eax
1:  cmpl $4095, %eax
    jae Lmem_error
    cmpb $0, (%rsi,%rax)
    je 2f
    incl %eax
    jmp 1b
2:  MEM_LEAVE
#else
    movq %rdi, %rsi
    leaq Lexecutable_symlink(%rip), %rdi
    movl $4095, %edx
    movq %rsi, -32(%rbp)
    call LIB(readlink)
    cmpq $0, %rax
    jl Lmem_error
    cmpq $4095, %rax
    jae Lmem_error
    movq -32(%rbp), %rdi
    movb $0, (%rdi,%rax)
    MEM_LEAVE
#endif
.globl PUBLIC(vpp_native_os_home)
PUBLIC(vpp_native_os_home):
    MEM_ENTER
    CHECK_PATH %edi
    leaq (%r11,%rdi), %rax
    movq %rax, -32(%rbp)
    leaq Lhome_key(%rip), %rdi
    call LIB(getenv)
    testq %rax, %rax
    je Lmem_error
    movq %rax, %rsi
    movq -32(%rbp), %rdi
    call Lcopy_path
    MEM_LEAVE
.globl PUBLIC(vpp_native_os_cwd)
PUBLIC(vpp_native_os_cwd):
    MEM_ENTER
    CHECK_PATH %edi
    leaq (%r11,%rdi), %rdi
    movq %rdi, -32(%rbp)
    movl $4096, %esi
    call LIB(getcwd)
    testq %rax, %rax
    je Lmem_error
    movq -32(%rbp), %rsi
    movq %rsi, %rdi
    xorl %eax, %eax
1:  cmpl $4096, %eax
    jae Lmem_error
    cmpb $0, (%rdi,%rax)
    je 2f
    incl %eax
    jmp 1b
2:  MEM_LEAVE

.globl PUBLIC(vpp_native_sha_begin)
PUBLIC(vpp_native_sha_begin):
    MEM_ENTER
    leaq 3904(%r11), %rdi
#ifdef __APPLE__
    call LIB(CC_SHA256_Init)
#else
    call LIB(SHA256_Init)       // Requires libcrypto at final link
#endif
    cmpl $1, %eax
    je 1f
    movl $-1, %eax
    MEM_LEAVE
1:  xorl %eax, %eax
    MEM_LEAVE
.globl PUBLIC(vpp_native_sha_update)
PUBLIC(vpp_native_sha_update):
    MEM_ENTER
    CHECK_OFFSET %edi, 0
    cmpl $0, %esi
    jl Lmem_error
    movl %edi, %eax
    addq %rsi, %rax
    cmpq $VPP_ARENA_SIZE, %rax
    ja Lmem_error
    movl %esi, %edx
    leaq (%r11,%rdi), %rsi
    leaq 3904(%r11), %rdi
#ifdef __APPLE__
    call LIB(CC_SHA256_Update)
#else
    call LIB(SHA256_Update)
#endif
    cmpl $1, %eax
    jne Lsha_fail
    xorl %eax, %eax
    MEM_LEAVE
.globl PUBLIC(vpp_native_sha_end)
PUBLIC(vpp_native_sha_end):
    MEM_ENTER
    CHECK_OFFSET %edi, 32
    leaq 3904(%r11), %rsi
    leaq (%r11,%rdi), %rdi
#ifdef __APPLE__
    call LIB(CC_SHA256_Final)
#else
    call LIB(SHA256_Final)
#endif
    cmpl $1, %eax
    jne Lsha_fail
    xorl %eax, %eax
    MEM_LEAVE
Lsha_fail:
    movl $-1, %eax
    MEM_LEAVE

.globl PUBLIC(vpp_native_os_register_cache_report)
PUBLIC(vpp_native_os_register_cache_report):
    leaq PUBLIC(vpp_native_cache_report_entry)(%rip), %rdi
    jmp LIB(atexit)
.globl PUBLIC(vpp_native_i32_div)
PUBLIC(vpp_native_i32_div):
    testl %esi, %esi
    je 1f
    cmpl $-1, %esi
    jne 2f
    cmpl $0x80000000, %edi
    je 3f
2:  movl %edi, %eax
    cltd
    idivl %esi
    ret
1:  movl $-1, %eax
    ret
3:  movl $0x80000000, %eax
    ret

// macOS exposes argc/argv via libSystem. Linux/glibc preserves initial stack
// via __libc_stack_end; musl needs an explicit startup adapter in future.
.globl PUBLIC(vpp_native_argc)
PUBLIC(vpp_native_argc):
#ifdef __APPLE__
    subq $8, %rsp
    call LIB(_NSGetArgc)
    movl (%rax), %eax
    addq $8, %rsp
#else
    movq __libc_stack_end@GOTPCREL(%rip), %rax
    movq (%rax), %rax
    movl (%rax), %eax
#endif
    ret
Largument_ptr:
    cmpl $0, %edi
    jl 3f
    movl %edi, %r10d
#ifdef __APPLE__
    subq $24, %rsp
    movl %r10d, (%rsp)
    call LIB(_NSGetArgc)
    movl (%rax), %ecx
    cmpl %ecx, (%rsp)
    jae 2f
    call LIB(_NSGetArgv)
    movq (%rax), %rax
    movl (%rsp), %r10d
    movq (%rax,%r10,8), %rax
    addq $24, %rsp
    ret
2:  addq $24, %rsp
#else
    movq __libc_stack_end@GOTPCREL(%rip), %rax
    movq (%rax), %rax
    cmpl (%rax), %edi
    jae 3f
    movq 8(%rax,%r10,8), %rax
    ret
#endif
3:  xorl %eax, %eax
    ret
.globl PUBLIC(vpp_native_os_argument_length)
PUBLIC(vpp_native_os_argument_length):
    pushq %rbp
    movq %rsp, %rbp
    call Largument_ptr
    testq %rax, %rax
    je 2f
    xorl %edx, %edx
1:  cmpl $1048576, %edx
    ja 2f
    cmpb $0, (%rax,%rdx)
    je 3f
    incl %edx
    jmp 1b
3:  movl %edx, %eax
    popq %rbp
    ret
2:  movl $-1, %eax
    popq %rbp
    ret
.globl PUBLIC(vpp_native_os_argument_byte)
PUBLIC(vpp_native_os_argument_byte):
    pushq %rbp
    movq %rsp, %rbp
    pushq %rbx
    subq $8, %rsp
    movl %esi, %ebx
    call Largument_ptr
    testq %rax, %rax
    je 2f
    cmpl $1048576, %ebx
    jae 2f
    xorl %edx, %edx
1:  cmpb $0, (%rax,%rdx)
    je 2f
    cmpl %ebx, %edx
    je 3f
    incl %edx
    jmp 1b
3:  movzbl (%rax,%rdx), %eax
    jmp 4f
2:  movl $-1, %eax
4:  addq $8, %rsp
    popq %rbx
    popq %rbp
    ret

// The same .vi algorithms are exported using the host's native i32 ABI.
// Stack ABI: arguments are packed at rsp before calling vpp_fn_rt_*.
.macro RT_EXPORT public, target, argc
    .globl \public
\public:
#if VPP_NATIVE_RUNTIME_STACK_ABI
    subq $(40), %rsp
    .if \argc > 0
    movl %edi, (%rsp)
    .endif
    .if \argc > 1
    movl %esi, 4(%rsp)
    .endif
    .if \argc > 2
    movl %edx, 8(%rsp)
    .endif
#ifdef __APPLE__
    call "\target"
#else
    call \target
#endif
    addq $(40), %rsp
    ret
#else
#ifdef __APPLE__
    jmp "\target"
#else
    jmp \target
#endif
#endif
.endm
RT_EXPORT PUBLIC(vpp_native_bytes_new), RT_TARGET(rt_bytes_new), 0
RT_EXPORT PUBLIC(vpp_native_bytes_length), RT_TARGET(rt_bytes_length), 1
RT_EXPORT PUBLIC(vpp_native_bytes_get), RT_TARGET(rt_bytes_get), 2
RT_EXPORT PUBLIC(vpp_native_bytes_append), RT_TARGET(rt_bytes_append), 2
RT_EXPORT PUBLIC(vpp_native_bytes_concat), RT_TARGET(rt_bytes_concat), 2
RT_EXPORT PUBLIC(vpp_native_bytes_equal), RT_TARGET(rt_bytes_equal), 2
RT_EXPORT PUBLIC(vpp_native_bytes_slice), RT_TARGET(rt_bytes_slice), 3
RT_EXPORT PUBLIC(vpp_native_bytes_find), RT_TARGET(rt_bytes_find), 2
RT_EXPORT PUBLIC(vpp_native_bytes_clear), RT_TARGET(rt_bytes_clear), 1
RT_EXPORT PUBLIC(vpp_native_bytes_print), RT_TARGET(rt_bytes_print), 1
RT_EXPORT PUBLIC(vpp_native_bytes_push_i32), RT_TARGET(rt_bytes_push_i32), 2
RT_EXPORT PUBLIC(vpp_native_bytes_load_i32), RT_TARGET(rt_bytes_load_i32), 2
RT_EXPORT PUBLIC(vpp_native_bytes_store_i32), RT_TARGET(rt_bytes_store_i32), 3
RT_EXPORT PUBLIC(vpp_native_bytes_free), RT_TARGET(rt_bytes_free), 1
RT_EXPORT PUBLIC(vpp_native_bytes_reserve_extra), RT_TARGET(rt_bytes_reserve), 2
RT_EXPORT PUBLIC(vpp_native_argv), RT_TARGET(rt_argv), 1
RT_EXPORT PUBLIC(vpp_native_file_open_read), RT_TARGET(rt_file_open_read), 1
RT_EXPORT PUBLIC(vpp_native_file_open_write), RT_TARGET(rt_file_open_write), 1
RT_EXPORT PUBLIC(vpp_native_file_read_block), RT_TARGET(rt_file_read_block), 2
RT_EXPORT PUBLIC(vpp_native_file_write_block), RT_TARGET(rt_file_write_block), 2
RT_EXPORT PUBLIC(vpp_native_file_read_byte), RT_TARGET(rt_file_read_byte), 1
RT_EXPORT PUBLIC(vpp_native_file_write_byte), RT_TARGET(rt_file_write_byte), 2
RT_EXPORT PUBLIC(vpp_native_file_close), RT_TARGET(rt_file_close), 1
RT_EXPORT PUBLIC(vpp_native_print_i32), RT_TARGET(rt_print_i32), 1
RT_EXPORT PUBLIC(vpp_native_read_i32), RT_TARGET(rt_read_i32), 0
RT_EXPORT PUBLIC(vpp_native_read_status), RT_TARGET(rt_read_status), 0
RT_EXPORT PUBLIC(vpp_native_runtime_module_path), RT_TARGET(rt_runtime_module_path), 0
RT_EXPORT PUBLIC(vpp_native_runtime_file_path), RT_TARGET(rt_runtime_file_path), 1
RT_EXPORT PUBLIC(vpp_native_object_services), RT_TARGET(rt_object_services), 3
RT_EXPORT PUBLIC(vpp_native_object_archive_entry), RT_TARGET(rt_object_archive_entry), 2
RT_EXPORT PUBLIC(vpp_native_object_archive_summary), RT_TARGET(rt_object_archive_summary), 1
RT_EXPORT PUBLIC(vpp_native_link_archive_single), RT_TARGET(rt_link_archive_single), 1
RT_EXPORT PUBLIC(vpp_native_link_validate_paths), RT_TARGET(rt_link_validate_paths), 2
RT_EXPORT PUBLIC(vpp_native_link_temporary_path), RT_TARGET(rt_link_temporary_path), 3
RT_EXPORT PUBLIC(vpp_native_link_temp_open), RT_TARGET(rt_link_temp_open), 1
RT_EXPORT PUBLIC(vpp_native_link_publish), RT_TARGET(rt_link_publish), 2
RT_EXPORT PUBLIC(vpp_native_link_spawn_wait), RT_TARGET(rt_link_spawn_wait), 0
RT_EXPORT PUBLIC(vpp_native_link_validate_files), RT_TARGET(rt_link_validate_files), 2
RT_EXPORT PUBLIC(vpp_native_link_executable_ready), RT_TARGET(rt_link_executable_ready), 1
RT_EXPORT PUBLIC(vpp_native_link_write_object), RT_TARGET(rt_link_write_object), 3
RT_EXPORT PUBLIC(vpp_native_cache_header_write), RT_TARGET(rt_cache_header_write), 2
RT_EXPORT PUBLIC(vpp_native_cache_header_check), RT_TARGET(rt_cache_header_check), 2
RT_EXPORT PUBLIC(vpp_native_cache_digest_hex), RT_TARGET(rt_cache_digest_hex), 0
RT_EXPORT PUBLIC(vpp_native_cache_read), RT_TARGET(rt_cache_read), 2
RT_EXPORT PUBLIC(vpp_native_cache_write), RT_TARGET(rt_cache_write), 2
RT_EXPORT PUBLIC(vpp_native_link_macho), RT_TARGET(rt_link_macho), 3
RT_EXPORT PUBLIC(vpp_native_link_macho_modules), RT_TARGET(rt_link_macho_modules), 3
RT_EXPORT PUBLIC(vpp_native_cache_report_entry), RT_TARGET(rt_cache_report), 0

#ifdef __APPLE__
.section __TEXT,__cstring,cstring_literals
#else
.section .rodata
#endif
Lhome_key: .asciz "VPP_HOME"
Lclang_path: .asciz "/usr/bin/clang"
#ifndef __APPLE__
Lexecutable_symlink: .asciz "/proc/self/exe"
#endif
.data
.p2align 3
PUBLIC(vpp_native_arena_base): .quad 0
#ifdef __linux__
.section .note.GNU-stack,"",@progbits
#endif

#ifndef __APPLE__
#include "vpp_vietnamese_runtime_symbols.inc"

#endif

#include "vpp_vm_f64_primitives.inc"
#include "vpp_vm_system_primitives.inc"
#include "vpp_vm_directory_primitives.inc"
#include "vpp_vm_process_primitives.inc"
#include "vpp_vm_socket_primitives.inc"
