#pragma once

#include <cstddef>
#include <vector>

#include "vpp/compiler/ir.h"
#include "vm/instruction.h"

namespace vietvm::compiler {

// The backend choice is part of the compilation artifact so migration progress
// can be asserted by tests instead of inferred from the shape of generated
// bytecode.
enum class BytecodeBackend {
    DirectIr,
    LegacyTokenBridge,
};

struct DirectIrSupport {
    bool supported = false;
    std::size_t fallbackRegions = 0;
};

// Reports whether the complete program can be emitted without materializing
// source tokens. Mixed direct/legacy emission is intentionally deferred until
// both backends share one explicit allocation/compiler-state/fixup context.
DirectIrSupport analyzeDirectIrSupport(const IrProgram &program);

// Emit the currently supported stack-IR cohort.  Callers must check
// analyzeDirectIrSupport() first.  Semantic symbol IDs are deliberately not
// copied into bytecode operands; this emitter owns a separate name-to-slot map.
std::vector<Instruction> emitDirectBytecode(const IrProgram &program,
                                            bool emitMainCall = true);

const char *bytecodeBackendName(BytecodeBackend backend) noexcept;

} // namespace vietvm::compiler
