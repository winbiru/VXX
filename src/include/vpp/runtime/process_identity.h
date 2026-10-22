#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "vpp/bytecode/instruction.h"
#include "vpp/runtime/value.h"

namespace vietvm::runtime {

// Identity values never carry OS pointers or foreign descriptors.
bool isProcessIdentityPrimitive(int opcode) noexcept;
StackValue executeProcessIdentityPrimitive(
    Opcode opcode, const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities);

} // namespace vietvm::runtime
