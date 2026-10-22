#pragma once

#include <unordered_set>
#include <string>
#include <vector>

#include "vpp/bytecode/instruction.h"
#include "vpp/runtime/value.h"

namespace vietvm::runtime {

bool isTimeLocalPrimitive(int opcode) noexcept;
// One i64 LE epoch argument; returns 64 LE bytes with tm fields and gmtoff.
// The caller owns civil-time validation, timezone formatting and policies.
StackValue executeTimeLocalPrimitive(
    Opcode opcode, const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities);

} // namespace vietvm::runtime
