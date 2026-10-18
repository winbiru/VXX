#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "vm/instruction.h"

namespace vietvm::native {

// Small platform JIT for AArch64 (macOS/Linux) and x86-64 (Linux/Windows).
// No VM handles or C++ exceptions cross the generated function ABI.
class MachineExpression {
public:
    static bool supported() noexcept;
    static std::unique_ptr<MachineExpression> tryCompile(
        const std::vector<Instruction> &bytecode, std::size_t start,
        const std::vector<bool> &entryPoints);
    static std::unique_ptr<MachineExpression> tryCompileBinary(int opcode);
    static std::unique_ptr<MachineExpression> tryCompileConditionalBranch();
    // Only straight-line, pure functions of 0..2 int32 parameters, including
    // integer arithmetic/comparisons and logical negation. Checked arithmetic
    // overflow returns nullopt so the VM preserves its diagnostics.
    static std::unique_ptr<MachineExpression> tryCompileLeaf(
        const std::vector<Instruction> &bytecode, int argc);
    // An executable allocation contains an acyclic graph of pure integer
    // functions. Static OP_GOI edges become AArch64 BL or x86-64 CALL rel32.
    static std::unique_ptr<MachineExpression> tryCompileCallBundle(
        const std::unordered_map<int, std::vector<Instruction>> &functions);
    bool hasNativeCalls(int functionId) const noexcept;
    std::size_t nativeCallDepth(int functionId) const noexcept;
    std::size_t nativeCallInstructionCount(int functionId) const noexcept;
    std::optional<int> executeNativeCall(int functionId, int arg0, int arg1) const;

    MachineExpression(const MachineExpression &) = delete;
    MachineExpression &operator=(const MachineExpression &) = delete;
    ~MachineExpression();

    std::size_t instructionCount() const noexcept { return instructionCount_; }
    int execute() const;
    int execute(int left, int right) const;
    std::size_t executeBranch(int condition, std::size_t falsePc,
                              std::size_t truePc) const;
    std::optional<int> executeLeaf(int arg0 = 0, int arg1 = 0) const;

private:
    MachineExpression(void *code, std::size_t allocatedBytes,
                      std::size_t instructionCount) noexcept;
    static std::unique_ptr<MachineExpression> fromWords(
        const std::vector<std::uint32_t> &words, std::size_t instructionCount);
    static std::unique_ptr<MachineExpression> fromBytes(
        const std::vector<std::uint8_t> &bytes, std::size_t instructionCount);
    struct CallEntry {
        std::size_t wordOffset = 0;
        std::size_t depth = 0;
        std::size_t instructionCount = 0;
        bool hasCalls = false;
    };
    std::unordered_map<int, CallEntry> callEntries_;
    void *code_;
    std::size_t allocatedBytes_;
    std::size_t instructionCount_;
};

} // namespace vietvm::native
