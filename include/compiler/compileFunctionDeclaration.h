#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "../vm/instruction.h"

namespace vietvm::compiler {

void compileFunctionDeclaration(
    const std::vector<std::string> &tokens,
    std::size_t &pos,
    std::vector<Instruction> &bytecode,
    std::unordered_map<std::string, int> &symTab,
    int &nextId,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    const std::string &namePrefix = "",
    const std::string &classOwner = "",
    const std::string &visibility = "công khai");

} // namespace vietvm::compiler
