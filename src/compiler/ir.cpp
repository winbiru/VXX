#include "vpp/compiler/ir.h"

#include <cstddef>
#include <utility>

namespace vietvm::compiler {
namespace {

IrOpcode lowerOpcode(vietvm::frontend::AstStatementKind kind) noexcept {
    using vietvm::frontend::AstStatementKind;
    switch (kind) {
        case AstStatementKind::Empty: return IrOpcode::NoOp;
        case AstStatementKind::Import: return IrOpcode::Import;
        case AstStatementKind::Function: return IrOpcode::DefineFunction;
        case AstStatementKind::Class: return IrOpcode::DefineClass;
        case AstStatementKind::Conditional: return IrOpcode::Conditional;
        case AstStatementKind::Loop: return IrOpcode::Loop;
        case AstStatementKind::Switch: return IrOpcode::Switch;
        case AstStatementKind::Return: return IrOpcode::Return;
        case AstStatementKind::Print: return IrOpcode::Print;
        case AstStatementKind::Break: return IrOpcode::Break;
        case AstStatementKind::Continue: return IrOpcode::Continue;
        case AstStatementKind::Throw: return IrOpcode::Throw;
        case AstStatementKind::Try: return IrOpcode::Try;
        case AstStatementKind::Block:
        case AstStatementKind::Expression:
        case AstStatementKind::Unknown:
            return IrOpcode::Statement;
    }
    return IrOpcode::Statement;
}

} // namespace

IrProgram lowerToIr(const vietvm::frontend::AstProgram &program,
                    const SemanticModel &semantic) {
    IrProgram ir;
    ir.instructions.reserve(program.statements.size());

    for (const vietvm::frontend::AstStatement &statement : program.statements) {
        IrInstruction instruction;
        instruction.opcode = lowerOpcode(statement.kind);
        instruction.span = statement.span;
        instruction.symbolId = semantic.symbolForDeclaration(statement.tokenBegin);

        const std::size_t begin = statement.tokenBegin;
        const std::size_t end = statement.tokenEnd;
        if (begin < end && end <= program.tokens.size()) {
            instruction.tokens.insert(instruction.tokens.end(),
                                      program.tokens.begin() + static_cast<std::ptrdiff_t>(begin),
                                      program.tokens.begin() + static_cast<std::ptrdiff_t>(end));
        }
        ir.instructions.push_back(std::move(instruction));
    }
    return ir;
}

std::vector<std::string> materializeIrTokens(const IrProgram &program) {
    std::vector<std::string> tokens;
    for (const IrInstruction &instruction : program.instructions) {
        for (const vietvm::frontend::Token &token : instruction.tokens) {
            tokens.push_back(token.lexeme);
        }
    }
    return tokens;
}

const char *irOpcodeName(IrOpcode opcode) noexcept {
    switch (opcode) {
        case IrOpcode::NoOp: return "noop";
        case IrOpcode::Import: return "import";
        case IrOpcode::DefineFunction: return "define_function";
        case IrOpcode::DefineClass: return "define_class";
        case IrOpcode::Conditional: return "conditional";
        case IrOpcode::Loop: return "loop";
        case IrOpcode::Switch: return "switch";
        case IrOpcode::Return: return "return";
        case IrOpcode::Print: return "print";
        case IrOpcode::Break: return "break";
        case IrOpcode::Continue: return "continue";
        case IrOpcode::Throw: return "throw";
        case IrOpcode::Try: return "try";
        case IrOpcode::Statement: return "statement";
    }
    return "statement";
}

} // namespace vietvm::compiler
