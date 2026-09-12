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
        case AstExpressionKind::ListLiteral: return "list_literal";
        case AstExpressionKind::Index: return "index";
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

const char *astVisibilityName(AstVisibility visibility) noexcept {
    switch (visibility) {
        case AstVisibility::Unspecified: return "unspecified";
        case AstVisibility::Public: return "public";
        case AstVisibility::Private: return "private";
        case AstVisibility::Protected: return "protected";
    }
    return "unspecified";
}

const char *astImportFormName(AstImportForm form) noexcept {
    switch (form) {
        case AstImportForm::Unstructured: return "unstructured";
        case AstImportForm::LocalSourceFile: return "local_source_file";
    }
    return "unstructured";
}

const char *astClassFormName(AstClassForm form) noexcept {
    switch (form) {
        case AstClassForm::Unstructured: return "unstructured";
        case AstClassForm::MethodBlock: return "method_block";
    }
    return "unstructured";
}

const char *astConditionalFormName(AstConditionalForm form) noexcept {
    switch (form) {
        case AstConditionalForm::Unstructured: return "unstructured";
        case AstConditionalForm::IfBlock: return "if_block";
        case AstConditionalForm::IfElseBlocks: return "if_else_blocks";
    }
    return "unstructured";
}

const char *astLoopFormName(AstLoopForm form) noexcept {
    switch (form) {
        case AstLoopForm::Unstructured: return "unstructured";
        case AstLoopForm::ForBlock: return "for_block";
    }
    return "unstructured";
}

const char *astSwitchFormName(AstSwitchForm form) noexcept {
    switch (form) {
        case AstSwitchForm::Unstructured: return "unstructured";
        case AstSwitchForm::Structured: return "structured";
    }
    return "unstructured";
}

const char *astSwitchArmKindName(AstSwitchArmKind kind) noexcept {
    switch (kind) {
        case AstSwitchArmKind::Case: return "case";
        case AstSwitchArmKind::Default: return "default";
    }
    return "case";
}

const char *astTryFormName(AstTryForm form) noexcept {
    switch (form) {
        case AstTryForm::Unstructured: return "unstructured";
        case AstTryForm::TryCatchBlocks: return "try_catch_blocks";
    }
    return "unstructured";
}

} // namespace vietvm::frontend
