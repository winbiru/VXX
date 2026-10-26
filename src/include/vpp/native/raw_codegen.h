#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "vpp/compiler/ir.h"
#include "vpp/native/target.h"

namespace vietvm::native {

// Native leaf ABI: signed 32-bit result; binary arithmetic takes two signed 32-bit
// arguments in the platform's ordinary integer argument registers/stack.
// The byte stream is a FUNCTION BODY, not an ELF/COFF/Mach-O object.
enum class RawLeafKind { ReturnConstantI32, AddI32Arguments, SubI32Arguments };

struct RawLeaf {
    RawLeafKind kind = RawLeafKind::ReturnConstantI32;
    std::int32_t constant = 0;
};

// Restricted typed machine IR for standalone i32 functions. Slots are fixed
// logical positions, not VM dynamic values. Backends may place them in frame
// memory or registers. Every expression uses an explicit bounded evaluation
// stack. Branch targets index the instruction vector.
enum class MachineOpcode {
    PushConstant, LoadSlot, StoreSlot, Drop,
    Add, Sub, Mul, Div, Mod, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    BranchZero, Jump, Call, Return, Trap,
};

struct MachineInstruction {
    MachineOpcode opcode = MachineOpcode::Drop;
    std::int32_t immediate = 0;
    std::size_t operand = 0; // frame slot or destination instruction index
    std::size_t depthBefore = 0; // evaluation stack height at this instruction
    std::string symbol; // resolved direct callee; only used by Call
};

struct MachineFunction {
    std::vector<MachineInstruction> instructions;
    std::size_t parameterCount = 0;
    std::size_t slotCount = 0;
    std::size_t maxStackDepth = 0;
};

struct MachineFunctionLowering {
    MachineFunction function;
    std::string error;
    explicit operator bool() const noexcept { return error.empty(); }
};

MachineFunctionLowering lowerMachineFunction(const compiler::IrProgram &ir,
                                             std::string_view functionName);

struct RawLeafResult {
    std::vector<std::uint8_t> bytes;
    std::string error;
    explicit operator bool() const noexcept { return error.empty() && !bytes.empty(); }
};

// A relocation references a direct callee at the call instruction's encoded
// displacement (x86_64) or instruction word (AArch64). Object writers own the
// platform-specific relocation type and addend.
struct MachineRelocation {
    std::size_t offset = 0;
    std::string symbol;
};

// Reject any unsupported statement/operand rather than silently emitting a
// function with different semantics or falling back to the VM.
struct RawLeafLowering {
    RawLeaf leaf;
    std::string error;
    explicit operator bool() const noexcept { return error.empty(); }
};

RawLeafLowering lowerRawLeaf(const compiler::IrProgram &ir,
                             std::string_view functionName);
RawLeafResult emitRawLeaf(const Target &target, const RawLeaf &leaf);
struct MachineEmission {
    std::vector<std::uint8_t> bytes;
    std::string assembly;
    std::string error;
    std::vector<MachineRelocation> relocations;
    explicit operator bool() const noexcept { return error.empty() && !bytes.empty(); }
};
MachineEmission emitMachineFunction(const Target &target,
                                    const MachineFunction &function);

// Human-readable ISA instructions corresponding to emitRawLeaf(). This is
// assembly source for inspection/assembling with a matching target assembler,
// NOT an executable, object, or disassembly of arbitrary bytes.
struct RawAssemblyResult {
    std::string text;
    std::string error;
    explicit operator bool() const noexcept { return error.empty() && !text.empty(); }
};
RawAssemblyResult emitRawAssembly(const Target &target, const RawLeaf &leaf,
                                  std::string_view symbol);
RawAssemblyResult emitMachineAssembly(const Target &target,
                                      const MachineEmission &emission,
                                      std::string_view symbol);

// Relocatable, symbol-exporting object for a self-contained i32 leaf.
// ELF on Linux, Mach-O on macOS, COFF on Windows. PPC64 ELFv1 includes a
// function descriptor relocation; there are no external references.
struct RawObjectResult {
    std::vector<std::uint8_t> bytes;
    std::string error;
    explicit operator bool() const noexcept { return error.empty() && !bytes.empty(); }
};
RawObjectResult emitRawObject(const Target &target, const RawLeaf &leaf,
                              std::string_view symbol);
RawObjectResult emitRawObjectBytes(const Target &target,
                                   const std::vector<std::uint8_t> &code,
                                   std::string_view symbol);
RawObjectResult emitRawObjectBytes(const Target &target,
                                   const std::vector<std::uint8_t> &code,
                                   std::string_view symbol,
                                   const std::vector<MachineRelocation> &relocations);

} // namespace vietvm::native
