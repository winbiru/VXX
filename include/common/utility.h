#pragma once
#include <string>
#include <vector>
#include <utility>

namespace vietvm::compiler {

    // trim helpers and token sequence extractors
    std::string trim(const std::string &s);

    // Token-name helpers shared by compiler entry points and the expression parser.
    // They retain the current support for Vietnamese multi-word identifiers.
    std::string joinNameTokens(const std::vector<std::string>& tokens, size_t begin, size_t end);
    bool isIdentifierLikeToken(const std::string &token);
    bool isCallableNamePiece(const std::string &token);

    // Extract functions operating on token vector
    // extractParens: tokens[start] must be "(" -> returns (content, indexAfterClosingParen)
    std::pair<std::string, size_t> extractParens(const std::vector<std::string>& tokens, size_t start);

    // extractBlock: tokens[start] must be "{" -> returns (content, indexAfterClosingBrace)
    std::pair<std::string, size_t> extractBlock(const std::vector<std::string>& tokens, size_t start);

    // extractExpressionUntilSemicolon: returns (exprString, indexAfterSemicolon)
    std::pair<std::string, size_t> extractExpressionUntilSemicolon(const std::vector<std::string>& tokens, size_t start);

    // extractAssignedVar: from an assignment expression string, return lhs trimmed
    std::string extractAssignedVar(const std::string& expr);

} // namespace vietvm::Compiler
