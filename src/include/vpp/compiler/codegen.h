#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "vpp/compiler/ir.h"
#include "vm/instruction.h"

namespace vietvm::compiler {

struct DirectIrSupport {
    bool supported = false;
    std::size_t unsupportedRegions = 0;
};

// Reports whether the complete program can be emitted by the direct emitter.
// Unsupported regions are rejected by the production compiler.
DirectIrSupport analyzeDirectIrSupport(const IrProgram &program);

// Emit the currently supported stack-IR cohort.  Callers must check
// analyzeDirectIrSupport() first.  Semantic symbol IDs are deliberately not
// copied into bytecode operands; this emitter owns a separate name-to-slot map.
std::vector<Instruction> emitDirectBytecode(const IrProgram &program,
                                            const std::unordered_map<std::string, Opcode> &keywordMap,
                                            bool emitMainCall = true);

} // namespace vietvm::compiler
