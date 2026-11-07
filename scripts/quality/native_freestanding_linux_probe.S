// Small executable used to verify the freestanding Linux syscall contract.
// Assembly only: no libc/libffi, CRT, C++ runtime, or compiler-generated code.
#if !defined(__linux__) || !defined(__x86_64__)
#error This probe requires Linux x86-64
#endif

// Vùng nhớ của bộ máy Linux x86-64 hiện tại, gồm cả vùng riêng của trình liên kết.
#define PROBE_ARENA_SIZE 135271424

.macro COPY_TO_ARENA offset, start, end
    movq vpp_native_arena_base(%rip), %rdi
    addq $\offset, %rdi
    leaq \start(%rip), %rsi
    movl $(\end-\start), %ecx
    cld
    rep movsb
.endm

.text
.p2align 4
.globl vpp_freestanding_main
.type vpp_freestanding_main, @function
vpp_freestanding_main:
    pushq %rbp
    movq %rsp, %rbp
    subq $256, %rsp
    // Child launched through fork/execve + PATH lookup by the compiler host.
    cmpl $2, %edi
    jne .Lmain_probe
    movq 8(%rsi), %r8
    testq %r8, %r8
    je .Lbad_arguments
    cmpl $0x636f7270, (%r8)       // "proc"
    jne .Lmain_childcheck
    cmpl $0x63737365, 4(%r8)      // "essc"
    jne .Lbad_arguments
    cmpl $0x6b636568, 8(%r8)      // "heck"
    jne .Lbad_arguments
    cmpb $0, 12(%r8)
    jne .Lbad_arguments
    call vpp_native_memory_base
    testq %rax, %rax
    je .Lbad_core
    COPY_TO_ARENA 40960, .Lprocess_env_name, .Lprocess_env_name_end
    movl $40960, %edi
    movl $45056, %esi
    call vpp_native_os_env_copy
    cmpl $10, %eax                 // childvalue
    jne .Lbad_core
    movq vpp_native_arena_base(%rip), %rax
    movabsq $0x6c6176646c696863, %rdx
    cmpq %rdx, 45056(%rax)       // "childval"
    jne .Lbad_core
    cmpw $0x6575, 45064(%rax)      // "ue"
    jne .Lbad_core
    movl $1, %edi
    leaq .Lprocess_stdout(%rip), %rsi
    movl $(.Lprocess_stdout_end-.Lprocess_stdout), %edx
    call vpp_sys_write
    movl $2, %edi
    leaq .Lprocess_stderr(%rip), %rsi
    movl $(.Lprocess_stderr_end-.Lprocess_stderr), %edx
    call vpp_sys_write
    xorl %eax, %eax
    leave
    ret
.Lmain_childcheck:
    cmpl $0x6c696863, (%r8)       // "chil"
    jne .Lbad_arguments
    cmpl $0x65686364, 4(%r8)      // "dche"
    jne .Lbad_arguments
    cmpw $0x6b63, 8(%r8)          // "ck"
    jne .Lbad_arguments
    cmpb $0, 10(%r8)
    jne .Lbad_arguments
    xorl %eax, %eax
    leave
    ret
.Lmain_probe:
    cmpl $3, %edi
    jne .Lbad_arguments
    cmpq $0, (%rsi)
    je .Lbad_arguments
    cmpq $0, (%rdx)
    je .Lbad_arguments
    cmpq %rsi, vpp_linux_argv(%rip)
    jne .Lbad_arguments
    cmpq %rdx, vpp_linux_envp(%rip)
    jne .Lbad_arguments
    cmpq $0, vpp_linux_initial_stack(%rip)
    je .Lbad_arguments

    // Call the actual V++ arena machine primitives, including bounds checks.
    call vpp_native_argc
    cmpl $3, %eax
    jne .Lbad_core
    movl $1, %edi
    call vpp_native_os_argument_length
    cmpl $5, %eax                 // argv[1] == "check"
    jne .Lbad_core
    movl $1, %edi
    xorl %esi, %esi
    call vpp_native_os_argument_byte
    cmpl $99, %eax                // 'c'
    jne .Lbad_core
    movl $4096, %edi
    movl $42, %esi
    call vpp_native_memory_write_i32
    testl %eax, %eax
    jne .Lbad_core
    movl $4096, %edi
    call vpp_native_memory_read_i32
    cmpl $42, %eax
    jne .Lbad_core
    movl $4100, %edi
    movl $255, %esi
    call vpp_native_memory_write_byte
    testl %eax, %eax
    jne .Lbad_core
    movl $4100, %edi
    call vpp_native_memory_read_byte
    cmpl $255, %eax
    jne .Lbad_core
    movl $PROBE_ARENA_SIZE, %edi
    call vpp_native_memory_read_byte
    cmpl $-1, %eax
    jne .Lbad_core
    movl $6144, %edi
    movl $4096, %esi
    call vpp_native_memory_bind
    testl %eax, %eax
    jne .Lbad_core
    movl $-1, %edi
    call vpp_native_os_close
    cmpl $-1, %eax
    jne .Lbad_core
    call vpp_native_os_errno
    cmpl $9, %eax                 // EBADF
    jne .Lbad_core

    // Shared V++ VM machine primitives use raw syscalls without libc.
    xorl %edi, %edi               // CLOCK_MONOTONIC
    movl $57344, %esi
    call vpp_native_os_clock
    testl %eax, %eax
    jne .Lbad_core
    movl $57352, %edi             // timespec.tv_nsec low 32 bits
    call vpp_native_memory_read_i32
    cmpl $1000000000, %eax
    jae .Lbad_core
    movl $57360, %edi
    call vpp_native_os_thread_cpu_clock
    testl %eax, %eax
    jne .Lbad_core
    xorl %edi, %edi
    call vpp_native_os_sleep
    testl %eax, %eax
    jne .Lbad_core
    movl $1000, %edi
    call vpp_native_os_sleep
    cmpl $22, %eax
    jne .Lbad_core
    movl $57376, %edi
    movl $16, %esi
    call vpp_native_os_entropy
    cmpl $1, %eax
    jl .Lbad_core
    cmpl $16, %eax
    jg .Lbad_core
    call vpp_freestanding_socket_probe
    testl %eax, %eax
    jne .Lbad_core
    movl $2, %edi
    movl $57344, %esi
    call vpp_native_os_clock
    cmpl $-22, %eax
    jne .Lbad_core

    // The environment is supplied by the kernel's initial stack, not libc.
    COPY_TO_ARENA 8192, .Lenv_name, .Lenv_name_end
    movl $8192, %edi
    movl $12288, %esi
    call vpp_native_os_env_copy
    cmpl $5, %eax                 // VPP_FREESTANDING_PROBE=ready
    jne .Lbad_core
    movl $12288, %edi
    call vpp_native_memory_read_byte
    cmpl $114, %eax               // 'r'
    jne .Lbad_core
    movl $24576, %edi
    call vpp_native_os_home
    cmpl $4, %eax                 // HOME=/tmp in test script
    jne .Lbad_core
    movl $28672, %edi
    call vpp_native_os_executable_path
    testl %eax, %eax
    jle .Lbad_core
    movl $28672, %edi
    call vpp_native_memory_read_byte
    cmpl $47, %eax                // absolute /proc/self/exe path
    jne .Lbad_core
    movl $28672, %edi
    call vpp_native_os_access_exec
    testl %eax, %eax
    jne .Lbad_core
    movl $12288, %edi
    call vpp_native_os_cwd
    testl %eax, %eax
    jle .Lbad_core

    // A full 4096-byte path with no terminating NUL is rejected.
    movq vpp_native_arena_base(%rip), %rdi
    addq $32768, %rdi
    movl $4096, %ecx
    movb $120, %al
    rep stosb
    movl $32768, %edi
    xorl %esi, %esi
    call vpp_native_os_open
    cmpl $-1, %eax
    jne .Lbad_core
    call vpp_native_os_errno
    cmpl $22, %eax                 // invalid arena path => EINVAL
    jne .Lbad_core

    // mmap has six SysV parameters. Verify the sixth is passed on the stack.
    xorq %rdi, %rdi
    movl $4096, %esi
    movl $3, %edx
    movl $0x22, %ecx
    movq $-1, %r8
    subq $16, %rsp
    movq $0, (%rsp)
    call vpp_sys_mmap
    addq $16, %rsp
    cmpq $-4095, %rax
    jae .Lbad_mmap
    movq %rax, -8(%rbp)
    movb $86, (%rax)
    cmpb $86, (%rax)
    jne .Lbad_mmap
    movq %rax, %rdi
    movl $4096, %esi
    call vpp_sys_munmap
    testq %rax, %rax
    jne .Lbad_mmap

    // openat / write / lseek / read / fstat / close / unlinkat round trip.
    movq $-100, %rdi              // AT_FDCWD
    leaq .Lfilename(%rip), %rsi
    movl $578, %edx               // O_RDWR | O_CREAT | O_TRUNC
    movl $384, %ecx               // 0600
    call vpp_sys_openat
    testq %rax, %rax
    js .Lbad_file
    movl %eax, -16(%rbp)

    movl %eax, %edi
    leaq .Lpayload(%rip), %rsi
    movl $(.Lpayload_end-.Lpayload), %edx
    call vpp_sys_write
    cmpq $(.Lpayload_end-.Lpayload), %rax
    jne .Lbad_file

    movl -16(%rbp), %edi
    xorq %rsi, %rsi
    xorq %rdx, %rdx
    call vpp_sys_lseek
    testq %rax, %rax
    jne .Lbad_file

    movl -16(%rbp), %edi
    leaq -256(%rbp), %rsi
    movl $(.Lpayload_end-.Lpayload), %edx
    call vpp_sys_read
    cmpq $(.Lpayload_end-.Lpayload), %rax
    jne .Lbad_file
    leaq .Lpayload(%rip), %r8
    leaq -256(%rbp), %r9
    xorl %ecx, %ecx
.Lcompare:
    cmpl $(.Lpayload_end-.Lpayload), %ecx
    je .Lread_ok
    movb (%r8,%rcx), %al
    cmpb %al, (%r9,%rcx)
    jne .Lbad_file
    incl %ecx
    jmp .Lcompare
.Lread_ok:
    movl -16(%rbp), %edi
    leaq -256(%rbp), %rsi
    call vpp_sys_fstat
    testq %rax, %rax
    jne .Lbad_file
    movl -16(%rbp), %edi
    call vpp_sys_close
    testq %rax, %rax
    jne .Lbad_file
    movq $-100, %rdi
    leaq .Lfilename(%rip), %rsi
    xorq %rdx, %rdx
    call vpp_sys_unlinkat
    testq %rax, %rax
    jne .Lbad_file

    // V++ path/file primitives: open, fstat, stat/lstat, rename and unlink.
    COPY_TO_ARENA 16384, .Lnative_filename, .Lnative_filename_end
    COPY_TO_ARENA 20480, .Lnative_renamed, .Lnative_renamed_end
    movl $16384, %edi
    movl $1537, %esi             // Darwin O_WRONLY|O_CREAT|O_TRUNC ABI
    call vpp_native_os_open
    testl %eax, %eax
    js .Lbad_core
    movl %eax, -20(%rbp)
    movl %eax, %edi
    movl $3584, %esi
    call vpp_native_os_fstat
    testl %eax, %eax
    jne .Lbad_core
    movl -20(%rbp), %edi
    call vpp_native_os_close
    testl %eax, %eax
    jne .Lbad_core
    movl $16384, %edi
    movl $3376, %esi
    call vpp_native_os_stat
    testl %eax, %eax
    jne .Lbad_core
    movl $3380, %edi             // V++ compact st_mode offset
    call vpp_native_memory_read_i32
    andl $61440, %eax            // S_IFMT
    cmpl $32768, %eax            // S_IFREG
    jne .Lbad_core
    // stat and fstat must retain matching device and inode identities in
    // the compact V++ header, even though Linux st_mode lives at +24.
    movq vpp_native_arena_base(%rip), %r8
    movl 3376(%r8), %eax
    cmpl %eax, 3584(%r8)
    jne .Lbad_core
    movq 3384(%r8), %rax
    cmpq %rax, 3592(%r8)
    jne .Lbad_core
    movl $16384, %edi
    movl $20480, %esi
    call vpp_native_os_rename
    testl %eax, %eax
    jne .Lbad_core
    movl $20480, %edi
    movl $3584, %esi
    call vpp_native_os_lstat
    testl %eax, %eax
    jne .Lbad_core
    movl $20480, %edi
    call vpp_native_os_unlink
    testl %eax, %eax
    jne .Lbad_core

    // Secure temporary files work directly through getrandom + openat.
    COPY_TO_ARENA 36864, .Lmk_template, .Lmk_template_end
    movl $36864, %edi
    call vpp_native_os_mkstemp
    testl %eax, %eax
    js .Lbad_core
    movl %eax, -20(%rbp)
    movl %eax, %edi
    movl $3584, %esi
    call vpp_native_os_fstat
    testl %eax, %eax
    jne .Lbad_core
    movl $3588, %edi
    call vpp_native_memory_read_i32
    testl $127, %eax             // no permissions outside 0600
    jne .Lbad_core
    movl -20(%rbp), %edi
    call vpp_native_os_close
    testl %eax, %eax
    jne .Lbad_core
    movl $36864, %edi
    call vpp_native_os_unlink
    testl %eax, %eax
    jne .Lbad_core

    // waitpid uses the Linux wait4 syscall and normalizes kernel errno.
    call vpp_sys_getpid
    movl %eax, %edi
    movl $3360, %esi
    call vpp_native_os_waitpid
    cmpl $-1, %eax               // a process is not its own child
    jne .Lbad_core
    call vpp_native_os_errno
    cmpl $10, %eax                // ECHILD
    jne .Lbad_core

    // Compiler-host process spawn reads the full-width argv table built by
    // vpp_native_memory_bind. PATH points to the probe executable directory.
    COPY_TO_ARENA 45056, .Lspawn_command, .Lspawn_command_end
    COPY_TO_ARENA 49152, .Lspawn_child_arg, .Lspawn_child_arg_end
    movl $0x08001400, %edi
    movl $45056, %esi
    call vpp_native_memory_bind
    testl %eax, %eax
    jne .Lbad_core
    movl $0x08001408, %edi
    movl $49152, %esi
    call vpp_native_memory_bind
    testl %eax, %eax
    jne .Lbad_core
    movl $0x08001410, %edi
    xorl %esi, %esi
    call vpp_native_memory_write_i32
    movl $0x08001414, %edi
    xorl %esi, %esi
    call vpp_native_memory_write_i32
    movl $0x08001400, %edi
    call vpp_native_os_spawn_clang
    cmpl $1, %eax
    jl .Lbad_core
    movl %eax, %edi
    movl $3360, %esi
    call vpp_native_os_waitpid
    testl %eax, %eax
    jle .Lbad_core
    movl $3360, %edi
    call vpp_native_memory_read_i32
    testl %eax, %eax             // WIFEXITED && WEXITSTATUS == 0
    jne .Lbad_core

    // execve failure must terminate the child with status 127; the parent
    // must be able to reap it rather than silently claiming a successful link.
    COPY_TO_ARENA 53248, .Lspawn_missing_command, .Lspawn_missing_command_end
    movl $0x04001400, %edi
    movl $53248, %esi
    call vpp_native_memory_bind
    testl %eax, %eax
    jne .Lbad_core
    movl $0x04001400, %edi
    call vpp_native_os_spawn_clang
    cmpl $1, %eax
    jl .Lbad_core
    movl %eax, %edi
    movl $3360, %esi
    call vpp_native_os_waitpid
    testl %eax, %eax
    jle .Lbad_core
    movl $3360, %edi
    call vpp_native_memory_read_i32
    cmpl $32512, %eax           // WEXITSTATUS == 127
    jne .Lbad_core

    // The process boundary must reject bad argv entries before forking.
    // Validate argv[1], rather than assuming only argv[0] is trustworthy.
    movq vpp_native_arena_base(%rip), %rax
    movq $1, 0x04001408(%rax)   // pointer outside the V++ arena
    movl $0x04001400, %edi
    call vpp_native_os_spawn_clang
    cmpl $-1, %eax
    jne .Lbad_core

    // V++ guest process API: spawn from validated arena offsets with a
    // child-only environment override; drain stdout/stderr then wait.
    COPY_TO_ARENA 8388640, .Lspawn_command, .Lspawn_command_end
    COPY_TO_ARENA 8392704, .Lprocess_child_arg, .Lprocess_child_arg_end
    COPY_TO_ARENA 8396800, .Lprocess_env_entry, .Lprocess_env_entry_end
    movq vpp_native_arena_base(%rip), %r8
    movl $8388640, 74800(%r8)
    movl $8392704, 74804(%r8)
    movl $8396800, 75200(%r8)
    movl $-1, 73732(%r8)
    movl $-1, 73736(%r8)
    movl $74800, 73740(%r8)
    movl $75000, 73744(%r8)
    movl $2, 73748(%r8)
    movl $75200, 73752(%r8)
    movl $1, 73772(%r8)
    movl $77824, 73784(%r8)
    // Spawn validation must return 0 with positive EINVAL in control+48.
    movl $0, 73748(%r8)          // argc = 0
    xorl %edi, %edi
    movl $73728, %esi
    call vpp_native_os_process
    testl %eax, %eax
    jne .Lbad_core
    movq vpp_native_arena_base(%rip), %r8
    cmpl $22, 73776(%r8)
    jne .Lbad_core
    movl $2, 73748(%r8)
    movb $0, 8396800(%r8)       // reject an invalid environment key
    xorl %edi, %edi
    movl $73728, %esi
    call vpp_native_os_process
    testl %eax, %eax
    jne .Lbad_core
    movq vpp_native_arena_base(%rip), %r8
    cmpl $22, 73776(%r8)
    jne .Lbad_core
    movb $'V', 8396800(%r8)
    xorl %edi, %edi
    movl $73728, %esi
    call vpp_native_os_process
    cmpl $1, %eax
    jne .Lbad_core
    movq vpp_native_arena_base(%rip), %r8
    movl $5000, 73756(%r8)
    movl $1, %edi
    movl $73728, %esi
    call vpp_native_os_process
    cmpl $1, %eax
    jl .Lbad_core
    cmpl $3, %eax
    jg .Lbad_core
    movq vpp_native_arena_base(%rip), %r8
    movl $86016, 73760(%r8)
    movl $128, 73764(%r8)
    movl $1, 73768(%r8)
    movl $2, %edi
    movl $73728, %esi
    call vpp_native_os_process
    cmpl $(.Lprocess_stdout_end-.Lprocess_stdout), %eax
    jne .Lbad_core
    movq vpp_native_arena_base(%rip), %rdi
    addq $86016, %rdi
    leaq .Lprocess_stdout(%rip), %rsi
    movl $(.Lprocess_stdout_end-.Lprocess_stdout), %ecx
    repe cmpsb
    jne .Lbad_core
    movl $2, %edi
    movl $73728, %esi
    call vpp_native_os_process     // drain stdout EOF and close read end
    testl %eax, %eax
    jne .Lbad_core
    movq vpp_native_arena_base(%rip), %r8
    movl $2, 73768(%r8)
    movl $2, %edi
    movl $73728, %esi
    call vpp_native_os_process
    cmpl $(.Lprocess_stderr_end-.Lprocess_stderr), %eax
    jne .Lbad_core
    movq vpp_native_arena_base(%rip), %rdi
    addq $86016, %rdi
    leaq .Lprocess_stderr(%rip), %rsi
    movl $(.Lprocess_stderr_end-.Lprocess_stderr), %ecx
    repe cmpsb
    jne .Lbad_core
    movl $2, %edi
    movl $73728, %esi
    call vpp_native_os_process
    testl %eax, %eax
    jne .Lbad_core
    movl $1, %edi
    movl $73728, %esi
    call vpp_native_os_process
    cmpl $4, %eax                // both pipes reached EOF
    jne .Lbad_core
    movl $3, %edi
    movl $73728, %esi
    call vpp_native_os_process
    testl %eax, %eax
    jne .Lbad_core
    movl $4, %edi
    movl $73728, %esi
    call vpp_native_os_process
    testl %eax, %eax
    jne .Lbad_core
    movl $1, %edi
    movl $(PROBE_ARENA_SIZE - 15), %esi // Khối điều khiển vượt cuối vùng nhớ
    call vpp_native_os_process
    cmpl $-22, %eax
    jne .Lbad_core

    // A pointer inside the arena still needs a bounded NUL terminator.
    movl $0x04001408, %edi
    movl $32768, %esi          // 4096 non-NUL bytes from path check above
    call vpp_native_memory_bind
    testl %eax, %eax
    jne .Lbad_core
    movl $0x04001400, %edi
    call vpp_native_os_spawn_clang
    cmpl $-1, %eax
    jne .Lbad_core

    // The argv table itself must terminate within its reserved 4096 slots.
    movq vpp_native_arena_base(%rip), %rdi
    leaq 49152(%rdi), %rax
    addq $0x04001400, %rdi
    movl $4096, %ecx
    cld
    rep stosq
    movl $0x04001400, %edi
    call vpp_native_os_spawn_clang
    cmpl $-1, %eax
    jne .Lbad_core

    // Raw directory/identity ABI: -errno, 16-byte handle, 8192-byte buffer.
    xorl %edi, %edi
    call vpp_native_os_identity
    movl %eax, -24(%rbp)
    call vpp_sys_getpid
    cmpl -24(%rbp), %eax
    jne .Lbad_core
    movl $6, %edi
    call vpp_native_os_identity
    cmpl $-22, %eax
    jne .Lbad_core
    movl $1, %edi
    call vpp_native_os_identity
    testl %eax, %eax
    jle .Lbad_core
    movl $2, %edi
    call vpp_native_os_identity
    testl %eax, %eax
    js .Lbad_core

    COPY_TO_ARENA 16384, .Lnative_directory, .Lnative_directory_end
    movl $4, %edi
    movl $16384, %esi
    xorl %edx, %edx
    call vpp_native_os_directory
    testl %eax, %eax
    jne .Lbad_core
    xorl %edi, %edi
    movl $16384, %esi
    movl $57392, %edx          // aligned 16-byte directory handle
    call vpp_native_os_directory
    testl %eax, %eax
    jne .Lbad_core
    movl $1, %edi
    movl $57392, %esi
    movl $58368, %edx          // 8192-byte dirent buffer
    call vpp_native_os_directory
    testl %eax, %eax
    jle .Lbad_core             // at least '.' and '..' must be returned
    cmpl $8192, %eax
    ja .Lbad_core
    movq vpp_native_arena_base(%rip), %r8
    cmpb $'.', 58368+19(%r8) // first linux_dirent64 name
    jne .Lbad_core
    movl $1, %edi
    movl $57392, %esi
    movl $68154369, %edx      // buffer cannot fit 8192 bytes
    call vpp_native_os_directory
    cmpl $-22, %eax
    jne .Lbad_core
    movl $2, %edi
    movl $57392, %esi
    xorl %edx, %edx
    call vpp_native_os_directory
    testl %eax, %eax
    jne .Lbad_core
    movl $1, %edi
    movl $57392, %esi
    movl $58368, %edx
    call vpp_native_os_directory
    cmpl $-9, %eax             // revoked handle reports EBADF
    jne .Lbad_core
    movl $3, %edi
    movl $16384, %esi
    xorl %edx, %edx
    call vpp_native_os_directory
    testl %eax, %eax
    jne .Lbad_core

    // Reject a nonterminated path before entering the Linux kernel.
    movl $3, %edi
    movl $32768, %esi         // previously filled with 4096 'x' bytes
    xorl %edx, %edx
    call vpp_native_os_directory
    cmpl $-22, %eax
    jne .Lbad_core
    xorl %edi, %edi
    movl $68158465, %esi
    movl $57392, %edx
    call vpp_native_os_directory
    cmpl $-22, %eax
    jne .Lbad_core

    // NIST SHA-256 known-answer vectors, including split updates and a
    // multi-block message. All hashing is scalar ASM with zero OpenSSL.
    call vpp_native_sha_begin
    testl %eax, %eax
    jne .Lbad_core
    movl $64512, %edi
    call vpp_native_sha_end
    testl %eax, %eax
    jne .Lbad_core
    movq vpp_native_arena_base(%rip), %rdi
    addq $64512, %rdi
    leaq .Lsha_empty_expected(%rip), %rsi
    movl $32, %ecx
    cld
    repe cmpsb
    jne .Lbad_core

    COPY_TO_ARENA 61440, .Lsha_abc, .Lsha_abc_end
    call vpp_native_sha_begin
    testl %eax, %eax
    jne .Lbad_core
    movl $61440, %edi
    movl $1, %esi
    call vpp_native_sha_update
    testl %eax, %eax
    jne .Lbad_core
    movl $61441, %edi
    movl $2, %esi
    call vpp_native_sha_update
    testl %eax, %eax
    jne .Lbad_core
    movl $64512, %edi
    call vpp_native_sha_end
    testl %eax, %eax
    jne .Lbad_core
    movq vpp_native_arena_base(%rip), %rdi
    addq $64512, %rdi
    leaq .Lsha_abc_expected(%rip), %rsi
    movl $32, %ecx
    cld
    repe cmpsb
    jne .Lbad_core

    movq vpp_native_arena_base(%rip), %rdi
    addq $61440, %rdi
    movl $100, %ecx
    movb $'a', %al
    cld
    rep stosb
    call vpp_native_sha_begin
    testl %eax, %eax
    jne .Lbad_core
    movl $61440, %edi
    movl $50, %esi
    call vpp_native_sha_update
    testl %eax, %eax
    jne .Lbad_core
    movl $61490, %edi
    movl $50, %esi
    call vpp_native_sha_update
    testl %eax, %eax
    jne .Lbad_core
    movl $64512, %edi
    call vpp_native_sha_end
    testl %eax, %eax
    jne .Lbad_core
    movq vpp_native_arena_base(%rip), %rdi
    addq $64512, %rdi
    leaq .Lsha_100a_expected(%rip), %rsi
    movl $32, %ecx
    cld
    repe cmpsb
    jne .Lbad_core
    movl $(PROBE_ARENA_SIZE - 2), %edi
    movl $3, %esi
    call vpp_native_sha_update
    cmpl $-1, %eax
    jne .Lbad_core
    movl $(PROBE_ARENA_SIZE - 31), %edi
    call vpp_native_sha_end
    cmpl $-1, %eax
    jne .Lbad_core

    leaq -256(%rbp), %rdi
    movl $256, %esi
    call vpp_sys_getcwd
    testq %rax, %rax
    jle .Lbad_file

    // Ensure Linux errors remain -errno and are never mistaken for pointers.
    movl $-1, %edi
    call vpp_sys_close
    cmpq $-9, %rax                 // EBADF
    jne .Lbad_errno

    movl $1, %edi
    leaq .Lmessage(%rip), %rsi
    movl $(.Lmessage_end-.Lmessage), %edx
    call vpp_sys_write
    cmpq $(.Lmessage_end-.Lmessage), %rax
    jne .Lbad_file
    xorl %eax, %eax
    leave
    ret
.Lbad_arguments:
    movl $10, %eax
    leave
    ret
.Lbad_mmap:
    movl $11, %eax
    leave
    ret
.Lbad_file:
    movl $12, %eax
    leave
    ret
.Lbad_errno:
    movl $13, %eax
    leave
    ret
.Lbad_core:
    movl $14, %eax
    leave
    ret
.size vpp_freestanding_main, .-vpp_freestanding_main

.section .rodata
.Lfilename: .asciz ".vpp-freestanding-syscall-probe.tmp"
.Lenv_name: .asciz "VPP_FREESTANDING_PROBE"
.Lenv_name_end:
.Lnative_filename: .asciz ".vpp-freestanding-native-probe.tmp"
.Lnative_filename_end:
.Lnative_renamed: .asciz ".vpp-freestanding-native-probe-renamed.tmp"
.Lnative_renamed_end:
.Lnative_directory: .asciz ".vpp-freestanding-native-probe-dir"
.Lnative_directory_end:
.Lmk_template: .asciz ".vpp-freestanding-mkstemp-XXXXXX"
.Lmk_template_end:
.Lspawn_command: .asciz "vpp-freestanding-probe"
.Lspawn_command_end:
.Lspawn_child_arg: .asciz "childcheck"
.Lspawn_child_arg_end:
.Lspawn_missing_command: .asciz "./.vpp-freestanding-no-such-compiler"
.Lspawn_missing_command_end:
.Lprocess_child_arg: .asciz "processcheck"
.Lprocess_child_arg_end:
.Lprocess_env_name: .asciz "VPP_PROCESS_OVERRIDE"
.Lprocess_env_name_end:
.Lprocess_env_entry: .asciz "VPP_PROCESS_OVERRIDE=childvalue"
.Lprocess_env_entry_end:
.Lprocess_stdout: .ascii "process stdout\n"
.Lprocess_stdout_end:
.Lprocess_stderr: .ascii "process stderr\n"
.Lprocess_stderr_end:
.Lpayload: .ascii "V++ freestanding read/write"
.Lpayload_end:
.Lsha_abc: .ascii "abc"
.Lsha_abc_end:
.Lsha_empty_expected:
    .byte 0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14
    .byte 0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24
    .byte 0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c
    .byte 0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55
.Lsha_abc_expected:
    .byte 0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea
    .byte 0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23
    .byte 0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c
    .byte 0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
.Lsha_100a_expected:
    .byte 0x28,0x16,0x59,0x78,0x88,0xe4,0xa0,0xd3
    .byte 0xa3,0x6b,0x82,0xb8,0x33,0x16,0xab,0x32
    .byte 0x68,0x0e,0xb8,0xf0,0x0f,0x8c,0xd3,0xb9
    .byte 0x04,0xd6,0x81,0x24,0x6d,0x28,0x5a,0x0e
.Lmessage: .ascii "V++ Linux ELF freestanding: OK\n"
.Lmessage_end:
.section .note.GNU-stack,"",@progbits
