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

// A tolerant recursive parser for the current language surface. It creates a
// structural statement AST plus an untyped expression arena. Token ranges stay
// available whenever a legacy construct cannot yet be represented safely.
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    AstProgram parseProgram();

private:
    AstStatement parseStatement(bool insideBlock);
    AstStatement parseBlock();
    bool tryParseStructuredSwitch(AstStatement &statement);
    AstStatementKind classify(std::size_t begin) const noexcept;
    std::string declarationName(std::size_t begin,
                                std::size_t end,
                                AstStatementKind kind) const;
    void attachDeclarationPayload(AstStatement &statement);
    void attachImportForm(AstStatement &statement);
    SourceSpan spanFor(std::size_t begin, std::size_t end) const noexcept;
    ExprId tryParseExpression(std::size_t begin, std::size_t end);
    bool tryParseLambdaBody(std::size_t begin,
                            std::size_t end,
                            AstStatement &body);
    void attachExpressionRoots(AstStatement &statement);
    void attachClassForm(AstStatement &statement);
    void attachConditionalForm(AstStatement &statement);
    void attachLoopForm(AstStatement &statement);
    void attachTryForm(AstStatement &statement);
    bool isCompound(AstStatementKind kind) const noexcept;
    bool isContinuation(std::size_t tokenIndex) const noexcept;

    std::vector<Token> tokens_;
    std::vector<AstExpression> expressions_;
    std::vector<AstLambda> lambdas_;
    std::size_t pos_ = 0;
};

AstProgram parseTokens(std::vector<Token> tokens);

} // namespace vietvm::frontend
