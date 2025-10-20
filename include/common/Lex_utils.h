#pragma once
// Lightweight lexical helpers for the compiler.
// Put helpers into vietvm::compiler namespace and expose them as free functions.

#include <string>
#include <unordered_map>
#include <vector>

namespace vietvm::compiler {

    bool isNumber(const std::string &s) noexcept;
    bool isOperator(const std::string &tok) noexcept;
    bool isStringLiteral(const std::string &tk) noexcept;
    bool isVariable(const std::string &tok) noexcept;
    std::vector<std::string> tokenize(const std::string &src);

    // Read-only access to operator precedence map.
    const std::unordered_map<std::string,int>& operatorPrecedenceMap() noexcept;

} // namespace vietvm::compiler