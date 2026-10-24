// Windows x64 (COFF, Microsoft x64 ABI), MSVC/UCRT and kernel32.
// The machine arena, byte operations, CRT file IO, argv and paths are live.
// POSIX-only linker primitives and SHA are intentionally fail-closed until
// the V++ runtime supports PE/COFF, Windows process creation and BCrypt.
// Assemble: clang -target x86_64-pc-windows-msvc -c this-file.S
#ifndef VPP_NATIVE_RUNTIME_STACK_ABI
#define VPP_NATIVE_RUNTIME_STACK_ABI 0
#endif
#define PUBLIC(x) x
#if VPP_NATIVE_RUNTIME_STACK_ABI
#define RT_TARGET(x) PUBLIC(vpp_fn_##x)
#else
#define RT_TARGET(x) PUBLIC(x)
#endif
#define VPP_ARENA_SIZE 68162560

.text
.p2align 4
.globl vpp_native_memory_base
vpp_native_memory_base:
    movq vpp_native_arena_base(%rip), %rax
    testq %rax, %rax
    jne 1f
    subq $40, %rsp          // 32-byte shadow space + call alignment
    xorl %ecx, %ecx
    movl $VPP_ARENA_SIZE, %edx
    movl $0x3000, %r8d      // MEM_RESERVE | MEM_COMMIT
    movl $4, %r9d           // PAGE_READWRITE
    call VirtualAlloc
    addq $40, %rsp
    testq %rax, %rax
    je 1f
    movq %rax, vpp_native_arena_base(%rip)
1:  ret

.macro MEM_ENTER
    pushq %rbp
    movq %rsp, %rbp
    subq $(96), %rsp       // includes shadow space
    movl %ecx, -4(%rbp)
    movl %edx, -8(%rbp)
    movl %r8d, -12(%rbp)
    call vpp_native_memory_base
    movq %rax, %r11
    movl -4(%rbp), %ecx
    movl -8(%rbp), %edx
    movl -12(%rbp), %r8d
    testq %r11, %r11
    je Lmem_error
.endm
.macro MEM_RETURN
    leave
    ret
.endm
.macro CHECK_OFFSET reg, width
    cmpl $(VPP_ARENA_SIZE - \width), \reg
    ja Lmem_error
.endm
.macro CHECK_PATH reg
    CHECK_OFFSET \reg, 4096
.endm
.globl vpp_native_memory_read_byte
vpp_native_memory_read_byte:
    MEM_ENTER
    CHECK_OFFSET %ecx, 1
    movzbl (%r11,%rcx), %eax
    MEM_RETURN
.globl vpp_native_memory_write_byte
vpp_native_memory_write_byte:
    MEM_ENTER
    CHECK_OFFSET %ecx, 1
    cmpl $255, %edx
    ja Lmem_error
    movb %dl, (%r11,%rcx)
    xorl %eax, %eax
    MEM_RETURN
.globl vpp_native_memory_read_i32
vpp_native_memory_read_i32:
    MEM_ENTER
    CHECK_OFFSET %ecx, 4
    movl (%r11,%rcx), %eax
    MEM_RETURN
.globl vpp_native_memory_write_i32
vpp_native_memory_write_i32:
    MEM_ENTER
    CHECK_OFFSET %ecx, 4
    movl %edx, (%r11,%rcx)
    xorl %eax, %eax
    MEM_RETURN
.globl vpp_native_memory_bind
vpp_native_memory_bind:
    MEM_ENTER
    CHECK_OFFSET %ecx, 8
    testl $7, %ecx
    jnz Lmem_error
    CHECK_OFFSET %edx, 1
    leaq (%r11,%rdx), %rax
    movq %rax, (%r11,%rcx)   // full 64-bit pointer, not i32 truncation
    xorl %eax, %eax
    MEM_RETURN
Lmem_error:
    movl $-1, %eax
    MEM_RETURN

.macro CRT_IO name, target
    .globl \name
\name:
    MEM_ENTER
    CHECK_OFFSET %edx, 0
    cmpl $(0), %r8d
    jl Lmem_error
    movl %edx, %eax
    addq %r8, %rax
    cmpq $(VPP_ARENA_SIZE), %rax
    ja Lmem_error
    leaq (%r11,%rdx), %rdx
    call \target
    MEM_RETURN
.endm
CRT_IO vpp_native_os_read, _read
CRT_IO vpp_native_os_write, _write
.globl vpp_native_os_open
vpp_native_os_open:
    MEM_ENTER
    CHECK_PATH %ecx
    leaq (%r11,%rcx), %rcx
    cmpl $0, %edx           // runtime.vi read mode
    je 1f
    cmpl $1537, %edx        // Darwin O_WRONLY|O_CREAT|O_TRUNC
    jne Lmem_error
    movl $0x8301, %edx      // _O_BINARY|_O_CREAT|_O_TRUNC|_O_WRONLY
    jmp 2f
1:  movl $0x8000, %edx      // _O_RDONLY|_O_BINARY
2:  movl $0x180, %r8d       // _S_IREAD|_S_IWRITE
    call _open
    MEM_RETURN
.globl vpp_native_os_close
vpp_native_os_close:
    jmp _close
.globl vpp_native_os_errno
vpp_native_os_errno:
    subq $40, %rsp
    call _errno
    movl (%rax), %eax
    addq $40, %rsp
    ret
.globl vpp_native_os_rename
vpp_native_os_rename:
    MEM_ENTER
    CHECK_PATH %ecx
    CHECK_PATH %edx
    leaq (%r11,%rcx), %rcx
    leaq (%r11,%rdx), %rdx
    call rename
    MEM_RETURN
.globl vpp_native_os_unlink
vpp_native_os_unlink:
    MEM_ENTER
    CHECK_PATH %ecx
    leaq (%r11,%rcx), %rcx
    call _unlink
    MEM_RETURN
.globl vpp_native_os_mkdir
vpp_native_os_mkdir:
    MEM_ENTER
    CHECK_PATH %ecx
    leaq (%r11,%rcx), %rcx
    call _mkdir
    MEM_RETURN
.globl vpp_native_os_access_exec
vpp_native_os_access_exec:
    MEM_ENTER
    CHECK_PATH %ecx
    leaq (%r11,%rcx), %rcx
    xorl %edx, %edx         // Windows _access supports only existence/read/write
    call _access
    MEM_RETURN

// rcx = destination, rdx = source; bounded ASCII/narrow-char copy.
Lcopy_path:
    xorl %eax, %eax
1:  cmpl $4095, %eax
    jae 2f
    movb (%rdx,%rax), %r9b
    movb %r9b, (%rcx,%rax)
    testb %r9b, %r9b
    je 3f
    incl %eax
    jmp 1b
2:  movl $-1, %eax
3:  ret
.globl vpp_native_os_env_copy
vpp_native_os_env_copy:
    MEM_ENTER
    CHECK_PATH %ecx
    CHECK_PATH %edx
    leaq (%r11,%rdx), %rax
    movq %rax, -32(%rbp)
    leaq (%r11,%rcx), %rcx
    call getenv
    testq %rax, %rax
    je Lmem_error
    movq %rax, %rdx
    movq -32(%rbp), %rcx
    call Lcopy_path
    MEM_RETURN
.globl vpp_native_os_home
vpp_native_os_home:
    MEM_ENTER
    CHECK_PATH %ecx
    leaq (%r11,%rcx), %rax
    movq %rax, -32(%rbp)
    leaq Lhome_key(%rip), %rcx
    call getenv
    testq %rax, %rax
    je Lmem_error
    movq %rax, %rdx
    movq -32(%rbp), %rcx
    call Lcopy_path
    MEM_RETURN
.globl vpp_native_os_cwd
vpp_native_os_cwd:
    MEM_ENTER
    CHECK_PATH %ecx
    leaq (%r11,%rcx), %rcx
    movq %rcx, -32(%rbp)
    movl $4096, %edx
    call _getcwd
    testq %rax, %rax
    je Lmem_error
    movq -32(%rbp), %rdx
    xorl %eax, %eax
1:  cmpl $4096, %eax
    jae Lmem_error
    cmpb $0, (%rdx,%rax)
    je 2f
    incl %eax
    jmp 1b
2:  MEM_RETURN
.globl vpp_native_os_executable_path
vpp_native_os_executable_path:
    MEM_ENTER
    CHECK_PATH %ecx
    leaq (%r11,%rcx), %rdx
    xorl %ecx, %ecx             // hModule=NULL -> current executable
    movl $4096, %r8d
    call GetModuleFileNameA      // Windows ANSI code page; UTF-8 adapter pending
    testl %eax, %eax
    je Lmem_error
    cmpl $4096, %eax
    jae Lmem_error
    MEM_RETURN

.globl vpp_native_argc
vpp_native_argc:
    subq $40, %rsp
    call __p___argc
    movl (%rax), %eax
    addq $40, %rsp
    ret
Largument_ptr:
    cmpl $0, %ecx
    jl 2f
    subq $56, %rsp
    movl %ecx, 48(%rsp)
    call __p___argc
    movl (%rax), %edx
    cmpl %edx, 48(%rsp)
    jae 1f
    call __p___argv
    movq (%rax), %rax
    movl 48(%rsp), %ecx
    movq (%rax,%rcx,8), %rax
    addq $56, %rsp
    ret
1:  addq $56, %rsp
2:  xorl %eax, %eax
    ret
.globl vpp_native_os_argument_length
vpp_native_os_argument_length:
    subq $40, %rsp
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
    addq $40, %rsp
    ret
2:  movl $-1, %eax
    addq $40, %rsp
    ret
.globl vpp_native_os_argument_byte
vpp_native_os_argument_byte:
    pushq %rbp
    movq %rsp, %rbp
    subq $48, %rsp
    movl %edx, -4(%rbp)
    call Largument_ptr
    testq %rax, %rax
    je 2f
    movl -4(%rbp), %ecx
    cmpl $1048576, %ecx
    jae 2f
    xorl %edx, %edx
1:  cmpb $0, (%rax,%rdx)
    je 2f
    cmpl %ecx, %edx
    je 3f
    incl %edx
    jmp 1b
3:  movzbl (%rax,%rdx), %eax
    jmp 4f
2:  movl $-1, %eax
4:  leave
    ret
.globl vpp_native_i32_div
vpp_native_i32_div:
    testl %edx, %edx
    je 1f
    cmpl $-1, %edx
    jne 2f
    cmpl $0x80000000, %ecx
    je 3f
2:  movl %ecx, %eax
    movl %edx, %ecx
    cltd
    idivl %ecx
    ret
1:  movl $-1, %eax
    ret
3:  movl $0x80000000, %eax
    ret

// Fail-closed entrypoints: require a PE/COFF-aware .vi linker and
// platform-specific structures, process ownership, and SHA-256 provider.
.macro UNSUPPORTED name
    .globl \name
\name:
    movl $(-1), %eax
    ret
.endm
UNSUPPORTED vpp_native_os_mkstemp
UNSUPPORTED vpp_native_os_spawn_clang
UNSUPPORTED vpp_native_os_waitpid
UNSUPPORTED vpp_native_os_stat
UNSUPPORTED vpp_native_os_lstat
UNSUPPORTED vpp_native_os_fstat
UNSUPPORTED vpp_native_sha_begin
UNSUPPORTED vpp_native_sha_update
UNSUPPORTED vpp_native_sha_end
.globl vpp_native_os_register_cache_report
vpp_native_os_register_cache_report:
    subq $40, %rsp
    leaq vpp_native_cache_report_entry(%rip), %rcx
    call atexit
    addq $40, %rsp
    ret

// Microsoft x64 native ABI adapters. The experimental V++ stack ABI stores
// arguments beyond the mandatory 32-byte shadow space.
.macro RT_EXPORT public, target, argc
    .globl \public
\public:
#if VPP_NATIVE_RUNTIME_STACK_ABI
    subq $(56), %rsp
    .if \argc > 0
    movl %ecx, 32(%rsp)
    .endif
    .if \argc > 1
    movl %edx, 36(%rsp)
    .endif
    .if \argc > 2
    movl %r8d, 40(%rsp)
    .endif
    call \target
    addq $(56), %rsp
    ret
#else
    jmp \target
#endif
.endm
#include "vpp_aot_runtime_exports.inc"

.section .rdata,"dr"
Lhome_key: .asciz "VPP_HOME"
.data
.p2align 3
vpp_native_arena_base: .quad 0

#include "vpp_vietnamese_runtime_symbols.inc"
