#include "vpp/compiler/optimizer.h"

#include <algorithm>

namespace vietvm::compiler {

OptimizationReport optimizeIr(IrProgram &program) {
    OptimizationReport report;
    const auto oldEnd = program.instructions.end();
    const auto newEnd = std::remove_if(program.instructions.begin(), oldEnd,
                                       [&](const IrInstruction &instruction) {
        if (instruction.opcode != IrOpcode::NoOp) return false;
        ++report.removedNoOps;
        return true;
    });
    program.instructions.erase(newEnd, oldEnd);
    return report;
}

} // namespace vietvm::compiler
