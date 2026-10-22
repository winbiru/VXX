#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "vpp/bytecode/instruction.h"
#include "vpp/runtime/value.h"

namespace vietvm::runtime {

// Shared OS clock boundary for machine consumers such as socket connect.
// Uses the same ASM clock path as the V++ monotonic intrinsic on Unix.
bool machineMonotonicMilliseconds(std::int64_t &milliseconds) noexcept;

bool isTimeClockPrimitive(int opcode) noexcept;
// Contract: list of 16 little-endian bytes, signed i64 seconds followed by
// signed i64 nanoseconds. Calendar and rounding policies belong to .vi.
StackValue executeTimeClockPrimitive(
    Opcode opcode, const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities);

} // namespace vietvm::runtime
