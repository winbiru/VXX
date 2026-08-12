#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "vpp/frontend/ast.h"

namespace vietvm::frontend {

class ParseError : public std::runtime_error {
public:
    ParseError(std::string message, SourceSpan span);

    const SourceSpan span;
};

// A tolerant recursive parser for the current language surface.  It creates a
// structural, span-carrying AST and leaves detailed expression lowering to the
// existing backend during the migration to a fully typed AST.
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    AstProgram parseProgram();

private:
    AstStatement parseStatement(bool insideBlock);
    AstStatement parseBlock();
    AstStatementKind classify(std::size_t begin) const noexcept;
    std::string declarationName(std::size_t begin,
                                std::size_t end,
                                AstStatementKind kind) const;
    SourceSpan spanFor(std::size_t begin, std::size_t end) const noexcept;
    bool isCompound(AstStatementKind kind) const noexcept;
    bool isContinuation(std::size_t tokenIndex) const noexcept;

    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
};

AstProgram parseTokens(std::vector<Token> tokens);

} // namespace vietvm::frontend
