#pragma once
// Lightweight lexical helpers for the Compiler.
// Put helpers into vietvm::Compiler namespace and expose them as free functions.

#include <string>
#include <unordered_map>
#include <vector>

namespace vietvm::compiler {

    bool isNumber(const std::string &s) noexcept;
    bool isOperator(const std::string &tok) noexcept;
    bool isStringLiteral(const std::string &tk) noexcept;
    bool isVariable(const std::string &tok) noexcept;
    std::vector<std::string> tokenize(const std::string &src);
    std::string stripQuotes(const std::string& input);

    // Read-only access to operator precedence map.
    const std::unordered_map<std::string,int>& operatorPrecedenceMap() noexcept;
    std::vector<std::string> postProcessTokens(const std::vector<std::string>& tokens);
    std::string normalizeTokenForCompare(const std::string& s);
    int getVarValueInt(int varId);
} // namespace vietvm::Compiler