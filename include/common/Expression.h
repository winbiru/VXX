#pragma once
#include <string>
#include <unordered_map>
#include <vector>

struct Instruction;

namespace vietvm::compiler {

    // Returns postfix token list for given infix tokens.
    std::vector<std::string> convertToPostfix(const std::vector<std::string>& infix_tokens);

    // inline std::unordered_map<int, std::vector<Instruction>> hamBytecodeMap;

    // precedence/associativity helpers
    int precedence_op(const std::string& op);
    char associativity_op(const std::string& op);

} // namespace vietvm::Compiler