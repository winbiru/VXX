#!/usr/bin/env python3
"""Exercise source assembly, executable semantics and atomic failure output.

Arguments: <compiler-with-assemble> <native-final-link-harness> <output-dir>.
ELF fixtures and the minimal host need no external assembler or linker.
"""
from pathlib import Path
import os
import subprocess
import sys

from check_native_elf_partial_linker import elf_object, packed

compiler, harness, directory = map(lambda p: Path(p).resolve(), sys.argv[1:])
directory.mkdir(parents=True, exist_ok=True)
host_root = directory / "minimal-host"
host = host_root / "scripts/ASM/linux/vpp_aot_primitives_linux_x86_64.o"
host.parent.mkdir(parents=True, exist_ok=True)
host.write_bytes(elf_object(bytes.fromhex("e80000000089c7b83c0000000f05"),
                            [(b"_start", 0)], [b"main"], [(1, b"main")]))
env = dict(os.environ, VPP_NATIVE_ELF_LINK="1", VPP_NATIVE_NO_CACHE="1")


def assemble(label, source, success=True):
    source_path = directory / (label + ".s")
    destination = directory / (label + ".o")
    source_path.write_text(source, encoding="utf-8")
    sentinel = b"previous output must survive an assembler failure\n"
    destination.write_bytes(sentinel)
    result = subprocess.run([str(compiler), "assemble", str(source_path), "-o", str(destination)],
                            env=env, capture_output=True, timeout=120)
    (directory / (label + ".log")).write_bytes(result.stdout + result.stderr)
    assert result.returncode == (0 if success else 1), (label, result.returncode, result.stdout)
    if not success:
        assert destination.read_bytes() == sentinel, label
    assert not list(directory.glob(destination.name + ".vpp-exe-*")), label
    return destination


def execute(label, source):
    obj = assemble(label, source)
    archive = directory / (label + ".archive")
    archive.write_bytes(packed(obj.read_bytes()))
    exe = directory / (label + ".exe")
    result = subprocess.run([str(harness), str(archive), str(exe)],
                            env=dict(env, VPP_HOME=str(host_root)), capture_output=True, timeout=120)
    assert result.returncode == 0, (label, result.stderr)
    assert subprocess.run([str(exe)], timeout=30).returncode == 42, label
    print("NATIVE_ASSEMBLER_" + label.upper().replace("-", "_") + "=PASS", flush=True)


execute("forward-backward-labels", """.text
.globl main
main:
movl $0, %eax
jmp 1f
2:
addl $6, %eax
ret
1:
movl $36, %eax
jmp 2b
""")
execute("data-rip-relative", """.text
.globl main
main:
movl answer(%rip), %eax
ret
.data
.p2align 2
answer:
.long 42
""")
execute("registers-sib-widths", """.text
.globl main
main:
pushq %r12
subq $32, %rsp
movq %rsp, %r12
movl $2, %eax
movw $36, 8(%r12,%rax,4)
movzwl 16(%r12), %eax
addl $6, %eax
addq $32, %rsp
popq %r12
ret
""")
execute("floating-point", """.text
.globl main
main:
movl $21, %eax
cvtsi2sdl %eax, %xmm0
addsd %xmm0, %xmm0
subq $16, %rsp
movsd %xmm0, (%rsp)
cvttsd2sil (%rsp), %eax
addq $16, %rsp
ret
""")
execute("rip-relative-immediate-tail", """.text
.globl main
main:
movl $42, answer(%rip)
movl answer(%rip), %eax
ret
.data
answer:
.long 0
""")
execute("absolute-relocation", """.text
.globl main
main:
movq pointer(%rip), %rax
movl (%rax), %eax
ret
.data
pointer:
.quad answer
answer:
.long 42
""")
execute("weak-undefined", """.text
.weak optional_function
.globl main
main:
leaq optional_function(%rip), %rax
testq %rax, %rax
jz 1f
call optional_function
ret
1:
movl $42, %eax
ret
""")
execute("rep-stosq", """.text
.globl main
main:
subq $32, %rsp
movq $99, (%rsp)
leaq (%rsp), %rdi
movl $1, %ecx
xorl %eax, %eax
rep stosq
movl (%rsp), %eax
addl $42, %eax
addq $32, %rsp
ret
""")

# jc/jnc are the carry/no-carry spellings for jb/jae. Their instruction
# encodings and relocations must match byte-for-byte, including branch fixups.
carry_source = """.text
.globl main
main:
movl $0, %eax
cmpl $1, %eax
{carry} 1f
movl $1, %eax
ret
1:
cmpl $0, %eax
{no_carry} 2f
movl $2, %eax
ret
2:
movl $42, %eax
ret
"""
carry_alias = assemble("carry-aliases", carry_source.format(carry="jc", no_carry="jnc"))
carry_canonical = assemble("carry-canonical", carry_source.format(carry="jb", no_carry="jae"))
assert carry_alias.read_bytes() == carry_canonical.read_bytes(), "carry condition aliases differ from canonical encodings"

# The executable exercises both carry flag outcomes using the native linker.
carry_archive = directory / "carry-aliases.archive"
carry_archive.write_bytes(packed(carry_alias.read_bytes()))
carry_exe = directory / "carry-aliases.exe"
carry_link = subprocess.run([str(harness), str(carry_archive), str(carry_exe)],
                            env=dict(env, VPP_HOME=str(host_root)), capture_output=True, timeout=120)
assert carry_link.returncode == 0, carry_link.stderr
assert subprocess.run([str(carry_exe)], timeout=30).returncode == 42, "carry aliases executed incorrectly"
print("NATIVE_ASSEMBLER_CARRY_ALIASES=PASS", flush=True)

invalid = {
    "unknown-instruction": ".text\nnot_an_instruction %eax\n",
    "duplicate-label": ".text\nmain:\nret\nmain:\nret\n",
    "missing-numeric-label": ".text\njmp 9f\n",
    "invalid-register": ".text\nmovl %r19d, %eax\n",
    "wrong-register-width": ".text\nmovq %eax, %rbx\n",
    "invalid-scale": ".text\nmovl (%rax,%rcx,3), %eax\n",
    "invalid-index": ".text\nmovl (%rax,%rsp,4), %eax\n",
    "unknown-section": ".section .unknown\n",
    "unsupported-section-flags": '.section .rodata,"aw",@progbits\n',
    "alignment-overflow": ".text\n.p2align 31\n",
    "oversize-immediate": ".text\nmovb $256, %al\n",
    "oversize-hex": ".text\nmovl $0x100000000, %eax\n",
    "oversize-negative-decimal": ".text\nmovl $-21474836480, %eax\n",
    "invalid-opcode-suffix": ".text\nmovx %eax, %ebx\n",
    "extra-syscall-operand": ".text\nsyscall %eax\n",
    "extra-memory-parenthesis": ".text\nmovl 4(%rax)), %eax\n",
    "wrong-extension-width": ".text\nmovzbl %rax, %eax\n",
    "wrong-setcc-width": ".text\nsetnz %eax\n",
    "wrong-sse-register": ".text\naddsd %rax, %xmm0\n",
    "malformed-expression": ".text\nmovl $(2 +), %eax\n",
    "unsupported-expression": ".text\nmovl $(2 * 4), %eax\n",
    "unbalanced-memory": ".text\nmovl 4(%rax, %eax\n",
    "unsupported-directive": ".include \"hidden.o\"\n",
}
for label, source in invalid.items():
    assemble(label, source, False)
    print("NATIVE_ASSEMBLER_REJECT_" + label.upper().replace("-", "_") + "=PASS", flush=True)

# Publishing an object must preserve its source, including filesystem aliases.
source = directory / "source-alias.s"
original = b".text\n.globl main\nmain:\nmovl $42, %eax\nret\n"
source.write_bytes(original)
hardlink = directory / "source-hardlink.o"
symlink = directory / "source-symlink.o"
for alias in (hardlink, symlink):
    if alias.exists() or alias.is_symlink():
        alias.unlink()
hardlink.hardlink_to(source)
symlink.symlink_to(source)
for label, destination in (("same-path", source), ("hardlink", hardlink), ("symlink", symlink)):
    result = subprocess.run([str(compiler), "assemble", str(source), "-o", str(destination)],
                            env=env, capture_output=True, timeout=120)
    assert result.returncode == 1, (label, result.stdout, result.stderr)
    assert source.read_bytes() == original, label
    assert not list(directory.glob(destination.name + ".vpp-exe-*")), label
    print("NATIVE_ASSEMBLER_REJECT_" + label.upper().replace("-", "_") + "=PASS", flush=True)
print("NATIVE_ASSEMBLER_REGRESSIONS=PASS", flush=True)
