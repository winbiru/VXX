#pragma once

#include <string>
#include <vector>

#include "../vm/instruction.h"

namespace vietvm::tooling {

std::string disassembleBytecode(const std::vector<Instruction> &bytecode,
                                const std::vector<std::string> &stringPool);

std::string formatSource(const std::string &source);

bool lintSource(const std::string &source, std::string &errorMessage);

} // namespace vietvm::tooling