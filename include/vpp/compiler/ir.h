#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "vpp/compiler/semantic.h"

namespace vietvm::compiler {

// The first IR is deliberately untyped. It provides a stable compiler-stage
// boundary while preserving the dynamic VM contract. Legacy lowering is a
// feature-migration fallback, independent of any future type-policy ADR.
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
using IrLambdaId = std::size_t;
inline constexpr IrLambdaId kInvalidIrLambdaId = static_cast<IrLambdaId>(-1);

// Keep the originating expression-arena identity on every IR value. Semantic
// resolution and lowering exchange bindings through this stable identity;
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
    Lambda,
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

    // `gọi tên(...)` has a distinct legacy forward-call contract from the
    // ordinary `tên(...)` form, so lowering preserves that source-level mode.
    bool explicitCall = false;
    CallTargetKind callTarget = CallTargetKind::Invalid;
    IrLambdaId lambdaId = kInvalidIrLambdaId;

    // Evaluation order is source order.  Calls store the callee first and then
    // their arguments.  Stores keep the target expression before the value.
    std::vector<IrValueId> operands;
};

struct IrParameter {
    std::string name;
    vietvm::frontend::SourceSpan span{};
    int symbolId = -1;
    bool hasDefault = false;
    IrValueId defaultValue = kInvalidIrValueId;
};

struct IrSwitchArm {
    vietvm::frontend::AstSwitchArmKind kind =
        vietvm::frontend::AstSwitchArmKind::Case;
    vietvm::frontend::SourceSpan span{};
    vietvm::frontend::SourceSpan labelSpan{};
    IrValueId label = kInvalidIrValueId;
    std::size_t bodyChildIndex = 0;
    bool hasColon = false;
    bool prefixedByCase = false;
};

struct IrInstruction {
    IrOpcode opcode = IrOpcode::Statement;
    vietvm::frontend::SourceSpan span{};
    int symbolId = -1;

    // Declaration metadata is explicit IR data. The direct backend must not
    // recover function names or parameter defaults by reparsing source tokens.
    std::string declarationName;
    vietvm::frontend::AstVisibility visibility =
        vietvm::frontend::AstVisibility::Unspecified;
    SemanticVisibility effectiveVisibility = SemanticVisibility::Unspecified;
    vietvm::frontend::AstClassForm classForm =
        vietvm::frontend::AstClassForm::Unstructured;
    vietvm::frontend::AstConditionalForm conditionalForm =
        vietvm::frontend::AstConditionalForm::Unstructured;
    vietvm::frontend::AstLoopForm loopForm =
        vietvm::frontend::AstLoopForm::Unstructured;
    vietvm::frontend::AstSwitchForm switchForm =
        vietvm::frontend::AstSwitchForm::Unstructured;
    vietvm::frontend::AstTryForm tryForm =
        vietvm::frontend::AstTryForm::Unstructured;
    std::string catchVariable;
    vietvm::frontend::SourceSpan catchVariableSpan{};
    int catchSymbolId = -1;
    std::vector<IrParameter> parameters;
    std::vector<IrSwitchArm> switchArms;
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

struct IrLambda {
    IrLambdaId id = kInvalidIrLambdaId;
    IrValueId ownerValue = kInvalidIrValueId;
    AstExprId sourceExprId = kInvalidAstExprId;
    vietvm::frontend::SourceSpan span{};
    std::vector<IrParameter> parameters;
    IrInstruction body;
    std::vector<int> captures;
};

struct IrProgram {
    std::vector<IrValue> values;
    std::vector<IrLambda> lambdas;
    std::vector<IrInstruction> instructions;
    std::size_t legacyRegionCount = 0;

    const IrValue *value(IrValueId id) const noexcept {
        return id < values.size() ? &values[id] : nullptr;
    }

    const IrLambda *lambda(IrLambdaId id) const noexcept {
        return id < lambdas.size() ? &lambdas[id] : nullptr;
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
