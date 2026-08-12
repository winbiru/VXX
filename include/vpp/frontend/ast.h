#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "vpp/frontend/token.h"

namespace vietvm::frontend {

// This is intentionally an untyped, lossless syntax AST.  The language is
// dynamically typed today, so type information belongs to a future policy and
// must not be fabricated in the parser.
enum class AstStatementKind {
    Empty,
    Block,
    Import,
    Function,
    Class,
    Conditional,
    Loop,
    Switch,
    Return,
    Print,
    Break,
    Continue,
    Throw,
    Try,
    Expression,
    Unknown,
};

struct AstStatement {
    AstStatementKind kind = AstStatementKind::Unknown;
    SourceSpan span{};

    // Ranges refer to AstProgram::tokens and keep the source representation
    // lossless while the legacy bytecode backend is being migrated.
    std::size_t tokenBegin = 0;
    std::size_t tokenEnd = 0;

    // Filled for declarations where the parser can identify a stable name.
    std::string declarationName;

    // Direct brace-delimited children.  A future expression AST can replace
    // token ranges without changing the public program/semantic boundaries.
    std::vector<AstStatement> children;
};

struct AstProgram {
    SourceSpan span{};
    std::vector<Token> tokens;
    std::vector<AstStatement> statements;
};

const char *astStatementKindName(AstStatementKind kind) noexcept;

} // namespace vietvm::frontend
