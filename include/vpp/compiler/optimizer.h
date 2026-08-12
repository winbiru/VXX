#pragma once

#include <cstddef>

#include "vpp/compiler/ir.h"

namespace vietvm::compiler {

struct OptimizationReport {
    std::size_t removedNoOps = 0;
};

// Optimizes the IR, not the runtime VM.  Bytecode peephole cleanup remains a
// backend detail while its transformations are migrated into IR passes.
OptimizationReport optimizeIr(IrProgram &program);

} // namespace vietvm::compiler
