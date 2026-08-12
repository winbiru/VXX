#include "vpp/frontend/ast.h"

namespace vietvm::frontend {

const char *astStatementKindName(AstStatementKind kind) noexcept {
    switch (kind) {
        case AstStatementKind::Empty: return "empty";
        case AstStatementKind::Block: return "block";
        case AstStatementKind::Import: return "import";
        case AstStatementKind::Function: return "function";
        case AstStatementKind::Class: return "class";
        case AstStatementKind::Conditional: return "conditional";
        case AstStatementKind::Loop: return "loop";
        case AstStatementKind::Switch: return "switch";
        case AstStatementKind::Return: return "return";
        case AstStatementKind::Print: return "print";
        case AstStatementKind::Break: return "break";
        case AstStatementKind::Continue: return "continue";
        case AstStatementKind::Throw: return "throw";
        case AstStatementKind::Try: return "try";
        case AstStatementKind::Expression: return "expression";
        case AstStatementKind::Unknown: return "unknown";
    }
    return "unknown";
}

const char *astExpressionKindName(AstExpressionKind kind) noexcept {
    switch (kind) {
        case AstExpressionKind::Literal: return "literal";
        case AstExpressionKind::Name: return "name";
        case AstExpressionKind::Unary: return "unary";
        case AstExpressionKind::Binary: return "binary";
        case AstExpressionKind::Assignment: return "assignment";
        case AstExpressionKind::CompoundAssignment: return "compound_assignment";
        case AstExpressionKind::Postfix: return "postfix";
        case AstExpressionKind::Call: return "call";
        case AstExpressionKind::Lambda: return "lambda";
        case AstExpressionKind::MapLiteral: return "map_literal";
    }
    return "unknown";
}

const char *astLiteralKindName(AstLiteralKind kind) noexcept {
    switch (kind) {
        case AstLiteralKind::None: return "none";
        case AstLiteralKind::Integer: return "integer";
        case AstLiteralKind::Float: return "float";
        case AstLiteralKind::String: return "string";
        case AstLiteralKind::Boolean: return "boolean";
        case AstLiteralKind::Null: return "null";
    }
    return "none";
}

} // namespace vietvm::frontend
