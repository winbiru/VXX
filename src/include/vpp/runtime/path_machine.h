#pragma once
#include <string>
#include <unordered_set>
#include <vector>
#include "vpp/bytecode/instruction.h"
#include "vpp/runtime/value.h"

namespace vietvm::runtime {
// Return legacy POSIX st_mode/S_IFMT >> 12 kind (4=dir, 8=file, 10=link)
// or Windows kind (0=other/reparse, 1=file, 2=dir), -1 if missing/error.
bool isPathMachinePrimitive(int opcode) noexcept;
StackValue executePathMachinePrimitive(Opcode opcode,
    const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities);
}
