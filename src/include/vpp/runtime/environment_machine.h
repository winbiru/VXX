#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "vpp/bytecode/instruction.h"
#include "vpp/runtime/value.h"

namespace vietvm::runtime {

bool isEnvironmentMachinePrimitive(int opcode) noexcept;
// Returns OS environment string or V++ null if absent. Policy is owned by .vi.
StackValue executeEnvironmentMachinePrimitive(
    Opcode opcode, const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities);

} // namespace vietvm::runtime
