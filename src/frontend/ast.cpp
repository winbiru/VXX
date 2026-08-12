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

} // namespace vietvm::frontend
