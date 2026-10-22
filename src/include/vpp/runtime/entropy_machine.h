#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "vpp/bytecode/instruction.h"
#include "vpp/runtime/value.h"

namespace vietvm::runtime {

bool isEntropyMachinePrimitive(int opcode) noexcept;
// One OS request, 1..256 bytes. The V++ library owns chunking and policy.
StackValue executeEntropyMachinePrimitive(
    Opcode opcode, const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities);

} // namespace vietvm::runtime
