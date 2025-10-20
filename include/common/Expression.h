#pragma once
#include <string>
#include <vector>

namespace vietvm::compiler {

    // Returns postfix token list for given infix tokens.
    std::vector<std::string> convertToPostfix(const std::vector<std::string>& infix_tokens);

    // precedence/associativity helpers
    int precedence_op(const std::string& op);
    char associativity_op(const std::string& op);

} // namespace vietvm::compiler