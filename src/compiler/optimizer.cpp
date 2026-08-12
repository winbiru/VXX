#include "vpp/compiler/optimizer.h"

#include <algorithm>
#include <vector>

namespace vietvm::compiler {
namespace {

void removeNoOps(std::vector<IrInstruction> &instructions,
                 OptimizationReport &report) {
    for (IrInstruction &instruction : instructions) {
        removeNoOps(instruction.children, report);
    }

    const auto oldEnd = instructions.end();
    const auto newEnd = std::remove_if(
        instructions.begin(), oldEnd,
        [&](const IrInstruction &instruction) {
            if (instruction.opcode != IrOpcode::NoOp) return false;
            ++report.removedNoOps;
            return true;
        });
    instructions.erase(newEnd, oldEnd);
}

} // namespace

OptimizationReport optimizeIr(IrProgram &program) {
    OptimizationReport report;
    removeNoOps(program.instructions, report);

    // Passes may remove a statement that carried a fallback marker, so the
    // public count is derived state rather than an incrementally maintained
    // counter.
    recomputeLegacyRegionCount(program);
    return report;
}

} // namespace vietvm::compiler
