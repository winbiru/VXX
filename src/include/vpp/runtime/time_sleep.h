#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "vpp/bytecode/instruction.h"
#include "vpp/runtime/value.h"

namespace vietvm::runtime {

bool isTimeSleepPrimitive(int opcode) noexcept;
StackValue executeTimeSleepPrimitive(
    Opcode opcode, const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities);

} // namespace vietvm::runtime
