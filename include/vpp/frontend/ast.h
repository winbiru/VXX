#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "vpp/frontend/token.h"

namespace vietvm::frontend {

using ExprId = std::size_t;
inline constexpr ExprId kInvalidExprId = static_cast<ExprId>(-1);

enum class AstExpressionKind {
    Literal,
    Name,
    Unary,
    Binary,
    Assignment,
    CompoundAssignment,
    Postfix,
    Call,
    Lambda,
    MapLiteral,
};

enum class AstLiteralKind {
    None,
    Integer,
    Float,
    String,
    Boolean,
    Null,
};

enum class AstVisibility {
    Unspecified,
    Public,
    Private,
    Protected,
};

struct AstParameter {
    std::string name;
    SourceSpan span{};
    bool hasDefault = false;
    ExprId defaultValue = kInvalidExprId;
};

struct AstMapEntry {
    ExprId key = kInvalidExprId;
    ExprId value = kInvalidExprId;
    SourceSpan span{};
};

// Expressions live in AstProgram::expressions.  ExprId is the stable arena
// index, so tree edges remain valid when the backing vector grows or moves.
// Every node also keeps its lossless token range for incremental migration of
// the legacy expression backend.
struct AstExpression {
    ExprId id = kInvalidExprId;
    AstExpressionKind kind = AstExpressionKind::Name;
    AstLiteralKind literalKind = AstLiteralKind::None;
    SourceSpan span{};
    std::size_t tokenBegin = 0;
    std::size_t tokenEnd = 0;

    // Literal spelling, name, or operator depending on kind.
    std::string text;

    ExprId operand = kInvalidExprId;
    ExprId left = kInvalidExprId;
    ExprId right = kInvalidExprId;
    ExprId callee = kInvalidExprId;
    std::vector<ExprId> arguments;

    std::vector<std::string> parameters;
    std::size_t bodyTokenBegin = 0;
    std::size_t bodyTokenEnd = 0;

    std::vector<AstMapEntry> mapEntries;
};

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
    AstVisibility visibility = AstVisibility::Unspecified;
    std::vector<AstParameter> parameters;

    // Expression roots owned by AstProgram::expressions. Most simple
    // statements have one root; loop headers may have several in source order.
    // An empty list means the tolerant parser retained only the token range.
    std::vector<ExprId> expressionRoots;

    // Direct brace-delimited statement children.
    std::vector<AstStatement> children;
};

struct AstProgram {
    SourceSpan span{};
    std::vector<Token> tokens;
    std::vector<AstExpression> expressions;
    std::vector<AstStatement> statements;

    const AstExpression *expression(ExprId id) const noexcept {
        return id < expressions.size() ? &expressions[id] : nullptr;
    }
};

const char *astStatementKindName(AstStatementKind kind) noexcept;
const char *astExpressionKindName(AstExpressionKind kind) noexcept;
const char *astLiteralKindName(AstLiteralKind kind) noexcept;

} // namespace vietvm::frontend
