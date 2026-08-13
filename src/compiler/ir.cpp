#include "vpp/compiler/ir.h"

#include <cstddef>
#include <limits>
#include <utility>

namespace vietvm::compiler {
namespace {

using vietvm::frontend::AstExpression;
using vietvm::frontend::AstExpressionKind;
using vietvm::frontend::AstLiteralKind;
using vietvm::frontend::AstProgram;
using vietvm::frontend::AstStatement;
using vietvm::frontend::AstStatementKind;
using vietvm::frontend::ExprId;

IrOpcode lowerOpcode(AstStatementKind kind) noexcept {
    switch (kind) {
        case AstStatementKind::Empty: return IrOpcode::NoOp;
        case AstStatementKind::Block: return IrOpcode::Block;
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
        case AstStatementKind::Expression:
        case AstStatementKind::Unknown:
            return IrOpcode::Statement;
    }
    return IrOpcode::Statement;
}

bool hasNameKind(const AstProgram &program, ExprId id) noexcept {
    const AstExpression *expression = program.expression(id);
    return expression != nullptr && expression->kind == AstExpressionKind::Name;
}

bool statementNeedsLegacyRegion(const AstStatement &statement) noexcept {
    const std::size_t expressionCount = statement.expressionRoots.size();
    switch (statement.kind) {
        case AstStatementKind::Empty:
        case AstStatementKind::Block:
        case AstStatementKind::Break:
        case AstStatementKind::Continue:
            return expressionCount != 0;

        case AstStatementKind::Conditional:
            return expressionCount != 1 || statement.children.empty();

        case AstStatementKind::Loop:
            return expressionCount != 3 || statement.children.empty();

        case AstStatementKind::Switch: {
            if (statement.switchForm != vietvm::frontend::AstSwitchForm::Structured ||
                expressionCount == 0 ||
                statement.switchArms.size() != statement.children.size()) {
                return true;
            }
            std::size_t caseCount = 0;
            for (const vietvm::frontend::AstSwitchArm &arm : statement.switchArms) {
                if (arm.bodyChildIndex >= statement.children.size() ||
                    statement.children[arm.bodyChildIndex].kind != AstStatementKind::Block) {
                    return true;
                }
                if (arm.kind == vietvm::frontend::AstSwitchArmKind::Case) {
                    ++caseCount;
                    if (arm.label == vietvm::frontend::kInvalidExprId) return true;
                } else if (arm.label != vietvm::frontend::kInvalidExprId) {
                    return true;
                }
            }
            return expressionCount != caseCount + 1;
        }

        case AstStatementKind::Return:
        case AstStatementKind::Throw:
            return expressionCount > 1;

        case AstStatementKind::Print:
        case AstStatementKind::Expression:
            return expressionCount != 1;

        case AstStatementKind::Function:
            if (expressionCount != 0 || statement.declarationName.empty() ||
                statement.children.size() != 1 ||
                statement.children.front().kind != AstStatementKind::Block) {
                return true;
            }
            for (const vietvm::frontend::AstParameter &parameter : statement.parameters) {
                if (parameter.name.empty() ||
                    (parameter.hasDefault &&
                     parameter.defaultValue == vietvm::frontend::kInvalidExprId)) {
                    return true;
                }
            }
            return false;

        case AstStatementKind::Class:
            return statement.classForm !=
                       vietvm::frontend::AstClassForm::MethodBlock ||
                   expressionCount != 0 || statement.children.size() != 1 ||
                   statement.children.front().kind != AstStatementKind::Block;

        case AstStatementKind::Try:
            return statement.tryForm !=
                       vietvm::frontend::AstTryForm::TryCatchBlocks ||
                   expressionCount != 0 || statement.children.size() != 2 ||
                   statement.children[0].kind != AstStatementKind::Block ||
                   statement.children[1].kind != AstStatementKind::Block;

        // These nodes still contain grammar-significant information only in
        // their token slice (for example import resolution and class member
        // roles). Their nested bodies are still
        // recursively lowered so migration can proceed inside the region.
        case AstStatementKind::Import:
        case AstStatementKind::Unknown:
            return true;
    }
    return true;
}

class Lowerer {
public:
    Lowerer(const AstProgram &program, const SemanticModel &semantic)
        : program_(program), semantic_(semantic), expressionMap_(program.expressions.size(),
          kInvalidIrValueId), expressionState_(program.expressions.size(), 0),
          cyclicExpression_(program.expressions.size(), false),
          expressionSymbols_(program.expressions.size(), -1),
          callBindings_(program.expressions.size(), nullptr) {
        for (const BindingResult &binding : semantic.expressionBindings) {
            if (binding.expression < expressionSymbols_.size()) {
                expressionSymbols_[binding.expression] = compatibleSymbolId(binding.symbol);
            }
        }
        for (const CallBinding &binding : semantic.callBindings) {
            if (binding.expression < callBindings_.size()) {
                callBindings_[binding.expression] = &binding;
            }
        }
    }

    IrProgram lower() {
        ir_.instructions.reserve(program_.statements.size());
        for (const AstStatement &statement : program_.statements) {
            ir_.instructions.push_back(lowerStatement(statement, true));
        }
        recomputeLegacyRegionCount(ir_);
        return std::move(ir_);
    }

private:
    static int compatibleSymbolId(SymbolId symbol) noexcept {
        if (symbol == kInvalidSymbolId ||
            symbol > static_cast<SymbolId>(std::numeric_limits<int>::max())) {
            return -1;
        }
        return static_cast<int>(symbol);
    }

    IrValueId appendLegacyValue(ExprId sourceExprId,
                                vietvm::frontend::SourceSpan span = {}) {
        IrValue value;
        value.id = ir_.values.size();
        value.opcode = IrValueOpcode::LegacyRegion;
        value.span = span;
        value.sourceExprId = sourceExprId;
        ir_.values.push_back(std::move(value));
        return ir_.values.back().id;
    }

    IrValueId lowerExpression(ExprId sourceId) {
        if (sourceId >= program_.expressions.size()) {
            return appendLegacyValue(sourceId);
        }

        if (expressionState_[sourceId] == 2) return expressionMap_[sourceId];
        if (expressionState_[sourceId] == 1) {
            const IrValueId existing = expressionMap_[sourceId];
            cyclicExpression_[sourceId] = true;
            if (existing < ir_.values.size()) {
                ir_.values[existing].opcode = IrValueOpcode::LegacyRegion;
            }
            return existing;
        }

        const AstExpression &source = program_.expressions[sourceId];
        const IrValueId resultId = ir_.values.size();
        expressionMap_[sourceId] = resultId;
        expressionState_[sourceId] = 1;

        IrValue placeholder;
        placeholder.id = resultId;
        placeholder.opcode = IrValueOpcode::LegacyRegion;
        placeholder.span = source.span;
        placeholder.sourceExprId = sourceId;
        placeholder.symbolId = expressionSymbols_[sourceId];
        placeholder.text = source.text;
        ir_.values.push_back(placeholder);

        IrValue result = std::move(placeholder);
        bool supported = true;

        auto addOperand = [&](ExprId operand) {
            if (operand == vietvm::frontend::kInvalidExprId) {
                supported = false;
                return;
            }
            result.operands.push_back(lowerExpression(operand));
        };

        switch (source.kind) {
            case AstExpressionKind::Literal:
                switch (source.literalKind) {
                    case AstLiteralKind::Integer:
                        result.opcode = IrValueOpcode::ConstInt;
                        break;
                    case AstLiteralKind::Float:
                        result.opcode = IrValueOpcode::ConstFloat;
                        break;
                    case AstLiteralKind::String:
                        result.opcode = IrValueOpcode::ConstString;
                        break;
                    case AstLiteralKind::Boolean:
                        result.opcode = IrValueOpcode::ConstBool;
                        break;
                    case AstLiteralKind::Null:
                        result.opcode = IrValueOpcode::ConstNull;
                        break;
                    case AstLiteralKind::None:
                        supported = false;
                        break;
                }
                break;

            case AstExpressionKind::Name:
                result.opcode = IrValueOpcode::LoadName;
                supported = !source.text.empty();
                break;

            case AstExpressionKind::Unary:
                result.opcode = IrValueOpcode::Unary;
                supported = !source.text.empty();
                addOperand(source.operand);
                break;

            case AstExpressionKind::Binary:
                result.opcode = IrValueOpcode::Binary;
                supported = !source.text.empty();
                addOperand(source.left);
                addOperand(source.right);
                break;

            case AstExpressionKind::Assignment:
                result.opcode = IrValueOpcode::StoreName;
                if (result.text.empty()) result.text = "=";
                supported = hasNameKind(program_, source.left);
                addOperand(source.left);
                addOperand(source.right);
                break;

            case AstExpressionKind::CompoundAssignment:
                result.opcode = IrValueOpcode::StoreName;
                supported = !source.text.empty() && hasNameKind(program_, source.left);
                addOperand(source.left);
                addOperand(source.right);
                break;

            case AstExpressionKind::Postfix:
                result.opcode = IrValueOpcode::StoreName;
                supported = !source.text.empty() && hasNameKind(program_, source.operand);
                addOperand(source.operand);
                break;

            case AstExpressionKind::Call:
                result.opcode = IrValueOpcode::CallDynamic;
                result.explicitCall = source.explicitCall;
                result.callTarget = CallTargetKind::DynamicName;
                supported = source.callee != vietvm::frontend::kInvalidExprId;
                addOperand(source.callee);
                for (ExprId argument : source.arguments) addOperand(argument);

                // Resolve only through the arena identity. Source spans and
                // textual scans are not unique enough to be semantic keys.
                if (const CallBinding *binding = callBindings_[sourceId]) {
                    result.symbolId = compatibleSymbolId(binding->symbol);
                    result.callTarget = binding->kind;
                    if (binding->kind == CallTargetKind::DirectFunction) {
                        result.opcode = IrValueOpcode::Call;
                    }
                    if (!binding->runtimeName.empty()) result.text = binding->runtimeName;
                }
                if (result.text.empty() && hasNameKind(program_, source.callee)) {
                    result.text = program_.expressions[source.callee].text;
                }
                break;

            case AstExpressionKind::Lambda:
                {
                    const vietvm::frontend::AstLambda *lambda =
                        program_.lambda(source.lambdaId);
                    if (lambda == nullptr || lambda->expression != sourceId ||
                        lambda->body.kind != AstStatementKind::Block) {
                        supported = false;
                        break;
                    }

                    result.opcode = IrValueOpcode::Lambda;
                    IrLambda lowered;
                    lowered.id = ir_.lambdas.size();
                    lowered.ownerValue = resultId;
                    lowered.sourceExprId = sourceId;
                    lowered.span = lambda->span;
                    result.lambdaId = lowered.id;

                    // Reserve the outer ID before recursively lowering
                    // defaults/body: either region may itself contain a
                    // lambda and append to this same arena.
                    ir_.lambdas.push_back(lowered);

                    const SemanticLambda *semanticLambda =
                        semantic_.lambdaForExpression(sourceId);
                    lowered.parameters.reserve(lambda->parameters.size());
                    for (std::size_t index = 0;
                         index < lambda->parameters.size(); ++index) {
                        const vietvm::frontend::AstParameter &parameter =
                            lambda->parameters[index];
                        IrParameter loweredParameter;
                        loweredParameter.name = parameter.name;
                        loweredParameter.span = parameter.span;
                        loweredParameter.hasDefault = parameter.hasDefault;
                        if (parameter.hasDefault) {
                            if (parameter.defaultValue ==
                                vietvm::frontend::kInvalidExprId) {
                                supported = false;
                            } else {
                                loweredParameter.defaultValue =
                                    lowerExpression(parameter.defaultValue);
                            }
                        }
                        if (semanticLambda != nullptr &&
                            index < semanticLambda->parameterSymbols.size()) {
                            loweredParameter.symbolId = compatibleSymbolId(
                                semanticLambda->parameterSymbols[index]);
                        }
                        lowered.parameters.push_back(
                            std::move(loweredParameter));
                    }
                    lowered.body = lowerStatement(lambda->body, false);
                    if (semanticLambda != nullptr) {
                        lowered.captures.reserve(semanticLambda->captures.size());
                        for (SymbolId capture : semanticLambda->captures) {
                            lowered.captures.push_back(compatibleSymbolId(capture));
                        }
                    }
                    ir_.lambdas[result.lambdaId] = std::move(lowered);
                }
                break;

            case AstExpressionKind::MapLiteral:
                result.opcode = IrValueOpcode::MapLiteral;
                for (const vietvm::frontend::AstMapEntry &entry : source.mapEntries) {
                    addOperand(entry.key);
                    addOperand(entry.value);
                }
                break;
        }

        if (!supported || cyclicExpression_[sourceId]) {
            result.opcode = IrValueOpcode::LegacyRegion;
        }

        ir_.values[resultId] = std::move(result);
        expressionState_[sourceId] = 2;
        return resultId;
    }

    IrInstruction lowerStatement(const AstStatement &statement, bool ownsTokens) {
        IrInstruction instruction;
        instruction.opcode = lowerOpcode(statement.kind);
        instruction.span = statement.span;
        instruction.declarationName = statement.declarationName;
        instruction.visibility = statement.visibility;
        instruction.classForm = statement.classForm;
        instruction.conditionalForm = statement.conditionalForm;
        instruction.loopForm = statement.loopForm;
        instruction.switchForm = statement.switchForm;
        instruction.tryForm = statement.tryForm;
        instruction.catchVariable = statement.catchVariable;
        instruction.catchVariableSpan = statement.catchVariableSpan;
        const auto declaration = semantic_.declarationSymbols.find(statement.tokenBegin);
        instruction.symbolId = declaration == semantic_.declarationSymbols.end()
            ? -1
            : declaration->second;
        if (instruction.symbolId >= 0 &&
            static_cast<std::size_t>(instruction.symbolId) < semantic_.symbols.size()) {
            const SemanticSymbol &symbol = semantic_.symbols[
                static_cast<std::size_t>(instruction.symbolId)];
            instruction.effectiveVisibility = symbol.visibility;
            if (statement.kind == AstStatementKind::Function &&
                symbol.kind == SemanticSymbolKind::Method &&
                !symbol.qualifiedName.empty()) {
                instruction.declarationName = symbol.qualifiedName;
            }
        }
        instruction.legacyRegion = statementNeedsLegacyRegion(statement);

        const ScopeId declarationScope = semantic_.scopeForStatement(statement.tokenBegin);
        instruction.parameters.reserve(statement.parameters.size());
        for (const vietvm::frontend::AstParameter &parameter : statement.parameters) {
            IrParameter lowered;
            lowered.name = parameter.name;
            lowered.span = parameter.span;
            lowered.hasDefault = parameter.hasDefault;
            if (parameter.defaultValue != vietvm::frontend::kInvalidExprId) {
                lowered.defaultValue = lowerExpression(parameter.defaultValue);
            }
            for (const SemanticSymbol &symbol : semantic_.symbols) {
                if (symbol.kind == SemanticSymbolKind::Parameter &&
                    symbol.declaringScope == declarationScope &&
                    symbol.lookupName == parameter.name) {
                    lowered.symbolId = compatibleSymbolId(symbol.id);
                    break;
                }
            }
            instruction.parameters.push_back(std::move(lowered));
        }

        instruction.expressionRoots.reserve(statement.expressionRoots.size());
        for (ExprId root : statement.expressionRoots) {
            instruction.expressionRoots.push_back(lowerExpression(root));
        }

        instruction.switchArms.reserve(statement.switchArms.size());
        for (const vietvm::frontend::AstSwitchArm &arm : statement.switchArms) {
            IrSwitchArm lowered;
            lowered.kind = arm.kind;
            lowered.span = arm.span;
            lowered.labelSpan = arm.labelSpan;
            lowered.bodyChildIndex = arm.bodyChildIndex;
            lowered.hasColon = arm.hasColon;
            lowered.prefixedByCase = arm.prefixedByCase;
            if (arm.label != vietvm::frontend::kInvalidExprId) {
                lowered.label = lowerExpression(arm.label);
            }
            instruction.switchArms.push_back(std::move(lowered));
        }

        if (!statement.catchVariable.empty() && statement.children.size() > 1) {
            const ScopeId bodyScope = semantic_.scopeForStatement(
                statement.children[1].tokenBegin);
            ScopeId catchScope = kInvalidScopeId;
            if (bodyScope < semantic_.scopes.size()) {
                catchScope = semantic_.scopes[bodyScope].parent;
            }
            for (const SemanticSymbol &symbol : semantic_.symbols) {
                if (symbol.kind == SemanticSymbolKind::CatchVariable &&
                    symbol.declaringScope == catchScope &&
                    symbol.lookupName == statement.catchVariable) {
                    instruction.catchSymbolId = compatibleSymbolId(symbol.id);
                    break;
                }
            }
        }

        instruction.children.reserve(statement.children.size());
        for (const AstStatement &child : statement.children) {
            instruction.children.push_back(lowerStatement(child, false));
        }

        if (ownsTokens) {
            const std::size_t begin = statement.tokenBegin;
            const std::size_t end = statement.tokenEnd;
            if (begin < end && end <= program_.tokens.size()) {
                instruction.tokens.insert(
                    instruction.tokens.end(),
                    program_.tokens.begin() + static_cast<std::ptrdiff_t>(begin),
                    program_.tokens.begin() + static_cast<std::ptrdiff_t>(end));
            } else if (begin != end || statement.kind != AstStatementKind::Empty) {
                instruction.legacyRegion = true;
            }
        }

        return instruction;
    }

    const AstProgram &program_;
    const SemanticModel &semantic_;
    IrProgram ir_;
    std::vector<IrValueId> expressionMap_;
    std::vector<unsigned char> expressionState_;
    std::vector<bool> cyclicExpression_;
    std::vector<int> expressionSymbols_;
    std::vector<const CallBinding *> callBindings_;
};

std::size_t countInstructionLegacyRegions(
    const IrProgram &program,
    const std::vector<IrInstruction> &instructions,
    std::vector<unsigned char> &visitedValues);

std::size_t countReachableLegacyValue(const IrProgram &program,
                                      IrValueId id,
                                      std::vector<unsigned char> &visited) {
    if (id >= program.values.size() || visited[id] != 0) return 0;
    visited[id] = 1;

    const IrValue &value = program.values[id];
    std::size_t count = value.opcode == IrValueOpcode::LegacyRegion ? 1 : 0;
    for (IrValueId operand : value.operands) {
        count += countReachableLegacyValue(program, operand, visited);
    }
    if (value.opcode == IrValueOpcode::Lambda) {
        const IrLambda *lambda = program.lambda(value.lambdaId);
        if (lambda != nullptr && lambda->ownerValue == id) {
            for (const IrParameter &parameter : lambda->parameters) {
                if (parameter.defaultValue != kInvalidIrValueId) {
                    count += countReachableLegacyValue(
                        program, parameter.defaultValue, visited);
                }
            }
            if (lambda->body.legacyRegion) ++count;
            for (IrValueId root : lambda->body.expressionRoots) {
                count += countReachableLegacyValue(program, root, visited);
            }
            count += countInstructionLegacyRegions(
                program, lambda->body.children, visited);
        }
    }
    return count;
}

std::size_t countInstructionLegacyRegions(
    const IrProgram &program,
    const std::vector<IrInstruction> &instructions,
    std::vector<unsigned char> &visitedValues) {
    std::size_t count = 0;
    for (const IrInstruction &instruction : instructions) {
        if (instruction.legacyRegion) ++count;
        for (const IrParameter &parameter : instruction.parameters) {
            if (parameter.defaultValue != kInvalidIrValueId) {
                count += countReachableLegacyValue(
                    program, parameter.defaultValue, visitedValues);
            }
        }
        for (IrValueId root : instruction.expressionRoots) {
            count += countReachableLegacyValue(program, root, visitedValues);
        }
        count += countInstructionLegacyRegions(
            program, instruction.children, visitedValues);
    }
    return count;
}

} // namespace

IrProgram lowerToIr(const vietvm::frontend::AstProgram &program,
                    const SemanticModel &semantic) {
    return Lowerer(program, semantic).lower();
}

std::vector<std::string> materializeIrTokens(const IrProgram &program) {
    std::vector<std::string> tokens;
    // Recursive children intentionally own no compatibility tokens.  Walking
    // only this root list therefore emits each source token exactly once.
    for (const IrInstruction &instruction : program.instructions) {
        for (const vietvm::frontend::Token &token : instruction.tokens) {
            tokens.push_back(token.lexeme);
        }
    }
    return tokens;
}

std::size_t recomputeLegacyRegionCount(IrProgram &program) {
    std::vector<unsigned char> visitedValues(program.values.size(), 0);
    const std::size_t count = countInstructionLegacyRegions(
        program, program.instructions, visitedValues);
    program.legacyRegionCount = count;
    return count;
}

const char *irOpcodeName(IrOpcode opcode) noexcept {
    switch (opcode) {
        case IrOpcode::NoOp: return "noop";
        case IrOpcode::Block: return "block";
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

const char *irValueOpcodeName(IrValueOpcode opcode) noexcept {
    switch (opcode) {
        case IrValueOpcode::LegacyRegion: return "legacy_region";
        case IrValueOpcode::ConstInt: return "const_int";
        case IrValueOpcode::ConstFloat: return "const_float";
        case IrValueOpcode::ConstString: return "const_string";
        case IrValueOpcode::ConstBool: return "const_bool";
        case IrValueOpcode::ConstNull: return "const_null";
        case IrValueOpcode::MapLiteral: return "map_literal";
        case IrValueOpcode::LoadName: return "load_name";
        case IrValueOpcode::StoreName: return "store_name";
        case IrValueOpcode::Unary: return "unary";
        case IrValueOpcode::Binary: return "binary";
        case IrValueOpcode::Call: return "call";
        case IrValueOpcode::CallDynamic: return "call_dynamic";
        case IrValueOpcode::Lambda: return "lambda";
    }
    return "legacy_region";
}

} // namespace vietvm::compiler
