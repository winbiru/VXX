// Test-only V++ memory arena for calling the native socket ABI from Python.
// Keep the same arena capacity and 16-byte alignment as the VM.
#if !defined(__linux__) || (!defined(__x86_64__) && !defined(__aarch64__))
#error Linux x86-64 or AArch64 required
#endif

.text
.globl vpp_native_memory_base
.type vpp_native_memory_base, @function
vpp_native_memory_base:
#if defined(__x86_64__)
    leaq .Ltest_memory(%rip), %rax
#else
    adrp x0, .Ltest_memory
    add x0, x0, :lo12:.Ltest_memory
#endif
    ret
.size vpp_native_memory_base, .-vpp_native_memory_base

.bss
.p2align 4
.Ltest_memory:
    .skip 135271424

.section .note.GNU-stack,"",@progbits
