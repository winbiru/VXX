#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "vpp/compiler/semantic.h"

namespace vietvm::compiler {

// The first IR is deliberately untyped.  It provides a stable compiler-stage
// boundary now, while preserving the dynamic VM contract and legacy bytecode
// lowering until a type-policy ADR is accepted.
enum class IrOpcode {
    NoOp,
    Block,
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

using IrValueId = std::size_t;
inline constexpr IrValueId kInvalidIrValueId = static_cast<IrValueId>(-1);

// Keep the originating expression-arena identity on every IR value.  This is
// the stable hook through which name resolution can attach bindings later;
// source spans are diagnostic data and are not unique expression identities.
using AstExprId = vietvm::frontend::ExprId;
inline constexpr AstExprId kInvalidAstExprId = vietvm::frontend::kInvalidExprId;

// Stack-oriented value operations.  Structured operations coexist with an
// explicit fallback marker while the legacy token backend is retired one
// expression family at a time.
enum class IrValueOpcode {
    LegacyRegion,
    ConstInt,
    ConstFloat,
    ConstString,
    ConstBool,
    ConstNull,
    MapLiteral,
    LoadName,
    StoreName,
    Unary,
    Binary,
    Call,
    CallDynamic,
};

struct IrValue {
    IrValueId id = kInvalidIrValueId;
    IrValueOpcode opcode = IrValueOpcode::LegacyRegion;
    vietvm::frontend::SourceSpan span{};
    AstExprId sourceExprId = kInvalidAstExprId;

    // Optional semantic binding.  It remains -1 until semantic analysis can
    // bind by sourceExprId rather than guessing from a source span.
    int symbolId = -1;

    // Literal spelling, name, or operator depending on opcode.
    std::string text;

    // Evaluation order is source order.  Calls store the callee first and then
    // their arguments.  Stores keep the target expression before the value.
    std::vector<IrValueId> operands;
};

struct IrInstruction {
    IrOpcode opcode = IrOpcode::Statement;
    vietvm::frontend::SourceSpan span{};
    int symbolId = -1;

    // Aligned with AstStatement::parameters for declarations. Parameters
    // without a default retain kInvalidIrValueId, preserving parameter index.
    std::vector<IrValueId> parameterDefaults;
    std::vector<IrValueId> expressionRoots;
    std::vector<IrInstruction> children;

    // True when this statement still needs the compatibility backend even if
    // some nested expressions or child statements have structured IR.
    bool legacyRegion = false;

    // Lossless compatibility payload.  Only top-level instructions own this
    // slice; recursive children are represented structurally and deliberately
    // do not duplicate their parent's source tokens.
    std::vector<vietvm::frontend::Token> tokens;
};

struct IrProgram {
    std::vector<IrValue> values;
    std::vector<IrInstruction> instructions;
    std::size_t legacyRegionCount = 0;

    const IrValue *value(IrValueId id) const noexcept {
        return id < values.size() ? &values[id] : nullptr;
    }
};

IrProgram lowerToIr(const vietvm::frontend::AstProgram &program,
                    const SemanticModel &semantic);

std::vector<std::string> materializeIrTokens(const IrProgram &program);

// Recalculate the number of explicitly marked fallback nodes after a pass has
// rewritten either the statement tree or value arena.
std::size_t recomputeLegacyRegionCount(IrProgram &program);

const char *irOpcodeName(IrOpcode opcode) noexcept;
const char *irValueOpcodeName(IrValueOpcode opcode) noexcept;

} // namespace vietvm::compiler
