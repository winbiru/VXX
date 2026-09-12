#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "vpp/frontend/token.h"

namespace vietvm::frontend {

using ExprId = std::size_t;
inline constexpr ExprId kInvalidExprId = static_cast<ExprId>(-1);
using LambdaId = std::size_t;
inline constexpr LambdaId kInvalidLambdaId = static_cast<LambdaId>(-1);

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
    ListLiteral,
    Index,
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

// Conditional shape is explicit syntax metadata, not inferred later from the
// number of parsed blocks. `Unstructured` keeps tolerant/legacy forms such as
// single-statement branches and `else if` on the compatibility path.
enum class AstConditionalForm {
    Unstructured,
    IfBlock,
    IfElseBlocks,
};

enum class AstLoopForm {
    Unstructured,
    ForBlock,
};

enum class AstClassForm {
    Unstructured,
    MethodBlock,
};

enum class AstImportForm {
    Unstructured,
    LocalSourceFile,
};

enum class AstSwitchForm {
    Unstructured,
    Structured,
};

enum class AstTryForm {
    Unstructured,
    TryCatchBlocks,
};

enum class AstSwitchArmKind {
    Case,
    Default,
};

// Switch bodies remain ordinary AstStatement children. Keeping only an index
// here avoids a recursive AstStatement/AstSwitchArm value type while giving
// every case label an explicit grammar role.
struct AstSwitchArm {
    AstSwitchArmKind kind = AstSwitchArmKind::Case;
    SourceSpan span{};
    SourceSpan labelSpan{};
    ExprId label = kInvalidExprId;
    std::size_t bodyChildIndex = 0;
    bool hasColon = false;
    bool prefixedByCase = false;
};

struct AstParameter {
    std::string name;
    SourceSpan span{};
    bool hasDefault = false;
    ExprId defaultValue = kInvalidExprId;
};

// Exact source metadata for imports whose target/alias shape is fully parsed.
// The target may be a .vi path or a bare/quoted package shortcut; resolution
// remains a compiler concern so parser metadata does not depend on the cwd.
struct AstImportSpec {
    std::string target;
    SourceSpan targetSpan{};
    bool quoted = false;
    std::string alias;
    SourceSpan aliasSpan{};
    bool hasSemicolon = false;
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

    // Preserve the source-level `gọi name(...)` form independently of the
    // node's token range. Grouping may widen tokenBegin/tokenEnd, but it must
    // not erase this grammar distinction before semantic lowering/codegen.
    bool explicitCall = false;
    std::vector<ExprId> arguments;

    // Lambda declarations live in AstProgram::lambdas. Keeping the recursive
    // statement body outside this expression node makes arena IDs stable and
    // avoids a recursive AstExpression/AstStatement value type.
    LambdaId lambdaId = kInvalidLambdaId;

    std::vector<AstMapEntry> mapEntries;
    std::vector<ExprId> listElements;
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
    AstImportForm importForm = AstImportForm::Unstructured;
    AstImportSpec importSpec;
    AstClassForm classForm = AstClassForm::Unstructured;
    AstConditionalForm conditionalForm = AstConditionalForm::Unstructured;
    AstLoopForm loopForm = AstLoopForm::Unstructured;
    AstSwitchForm switchForm = AstSwitchForm::Unstructured;
    AstTryForm tryForm = AstTryForm::Unstructured;
    std::string catchVariable;
    SourceSpan catchVariableSpan{};
    std::vector<AstParameter> parameters;
    std::vector<AstSwitchArm> switchArms;

    // Expression roots owned by AstProgram::expressions. Most simple
    // statements have one root; loop headers may have several in source order.
    // An empty list means the tolerant parser retained only the token range.
    std::vector<ExprId> expressionRoots;

    // Direct brace-delimited statement children.
    std::vector<AstStatement> children;
};

struct AstLambda {
    LambdaId id = kInvalidLambdaId;
    ExprId expression = kInvalidExprId;
    SourceSpan span{};
    std::vector<AstParameter> parameters;
    AstStatement body;
};

struct AstProgram {
    SourceSpan span{};
    std::vector<Token> tokens;
    std::vector<AstExpression> expressions;
    std::vector<AstLambda> lambdas;
    std::vector<AstStatement> statements;

    const AstExpression *expression(ExprId id) const noexcept {
        return id < expressions.size() ? &expressions[id] : nullptr;
    }

    const AstLambda *lambda(LambdaId id) const noexcept {
        return id < lambdas.size() ? &lambdas[id] : nullptr;
    }
};

const char *astStatementKindName(AstStatementKind kind) noexcept;
const char *astExpressionKindName(AstExpressionKind kind) noexcept;
const char *astLiteralKindName(AstLiteralKind kind) noexcept;
const char *astVisibilityName(AstVisibility visibility) noexcept;
const char *astImportFormName(AstImportForm form) noexcept;
const char *astClassFormName(AstClassForm form) noexcept;
const char *astConditionalFormName(AstConditionalForm form) noexcept;
const char *astLoopFormName(AstLoopForm form) noexcept;
const char *astSwitchFormName(AstSwitchForm form) noexcept;
const char *astSwitchArmKindName(AstSwitchArmKind kind) noexcept;
const char *astTryFormName(AstTryForm form) noexcept;

} // namespace vietvm::frontend
