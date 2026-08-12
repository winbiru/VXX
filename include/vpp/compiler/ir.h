#pragma once

#include <string>
#include <vector>

#include "vpp/compiler/semantic.h"

namespace vietvm::compiler {

// The first IR is deliberately untyped.  It provides a stable compiler-stage
// boundary now, while preserving the dynamic VM contract and legacy bytecode
// lowering until a type-policy ADR is accepted.
enum class IrOpcode {
    NoOp,
    Import,
    DefineFunction,
    DefineClass,
    Conditional,
    Loop,
    Switch,
    Return,
    Print,
    Break,
    Continue,
    Throw,
    Try,
    Statement,
};

struct IrInstruction {
    IrOpcode opcode = IrOpcode::Statement;
    vietvm::frontend::SourceSpan span{};
    int symbolId = -1;
    std::vector<vietvm::frontend::Token> tokens;
};

struct IrProgram {
    std::vector<IrInstruction> instructions;
};

IrProgram lowerToIr(const vietvm::frontend::AstProgram &program,
                    const SemanticModel &semantic);

std::vector<std::string> materializeIrTokens(const IrProgram &program);
const char *irOpcodeName(IrOpcode opcode) noexcept;

} // namespace vietvm::compiler
