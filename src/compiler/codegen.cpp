#include "vpp/compiler/codegen.h"

#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "common/storeString.h"
#include "common/utility.h"
#include "frontend/lexer.h"
#include "vpp/core/message_constants.h"

namespace vietvm::compiler {
namespace {

bool isBinaryOperator(const std::string &op) noexcept {
    return op == "+" || op == "-" || op == "*" || op == "/" || op == "%" ||
           op == "==" || op == "!=" || op == "<" || op == ">" ||
           op == "<=" || op == ">=" || op == "&&" || op == "||";
}

bool isCompoundAssignment(const std::string &op) noexcept {
    return op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=";
}

bool isExactSourceToken(const IrInstruction &instruction,
                        const IrValue &value) noexcept {
    for (const vietvm::frontend::Token &token : instruction.tokens) {
        if (token.span.begin.offset == value.span.begin.offset &&
            token.span.end.offset == value.span.end.offset &&
            token.lexeme == value.text) {
            return true;
        }
    }
    return false;
}

bool isExactSourceName(const IrInstruction &instruction,
                       const IrValue &value) {
    std::string sourceName;
    bool sawToken = false;
    std::size_t firstOffset = 0;
    std::size_t lastOffset = 0;
    for (const vietvm::frontend::Token &token : instruction.tokens) {
        if (token.span.end.offset <= value.span.begin.offset ||
            token.span.begin.offset >= value.span.end.offset) {
            continue;
        }
        if (token.span.begin.offset < value.span.begin.offset ||
            token.span.end.offset > value.span.end.offset) {
            return false;
        }
        if (!sawToken) {
            firstOffset = token.span.begin.offset;
            sawToken = true;
        } else {
            sourceName.push_back(' ');
        }
        sourceName += token.lexeme;
        lastOffset = token.span.end.offset;
    }
    return sawToken && firstOffset == value.span.begin.offset &&
           lastOffset == value.span.end.offset && sourceName == value.text;
}

bool endsWithSourceToken(const IrInstruction &instruction,
                         const IrValue &value,
                         const std::string &lexeme) noexcept {
    for (const vietvm::frontend::Token &token : instruction.tokens) {
        if (token.span.end.offset == value.span.end.offset) {
            return token.lexeme == lexeme;
        }
    }
    return false;
}

const vietvm::frontend::Token *firstTokenFor(
    const IrInstruction &sourceOwner,
    const IrInstruction &instruction) noexcept {
    for (const vietvm::frontend::Token &token : sourceOwner.tokens) {
        if (token.span.begin.offset == instruction.span.begin.offset) {
            return &token;
        }
    }
    return nullptr;
}

bool endsWithStatementToken(const IrInstruction &sourceOwner,
                            const IrInstruction &instruction,
                            const std::string &lexeme) noexcept {
    for (const vietvm::frontend::Token &token : sourceOwner.tokens) {
        if (token.span.end.offset == instruction.span.end.offset) {
            return token.lexeme == lexeme;
        }
    }
    return false;
}

bool hasExactStatementLexemes(
    const IrInstruction &sourceOwner,
    const IrInstruction &instruction,
    std::initializer_list<const char *> expected) noexcept {
    auto nextExpected = expected.begin();
    for (const vietvm::frontend::Token &token : sourceOwner.tokens) {
        if (token.span.begin.offset < instruction.span.begin.offset ||
            token.span.end.offset > instruction.span.end.offset) {
            continue;
        }
        if (nextExpected == expected.end() || token.lexeme != *nextExpected) {
            return false;
        }
        ++nextExpected;
    }
    return nextExpected == expected.end();
}

bool startsWithDedicatedCallSyntax(const IrInstruction &sourceOwner,
                                   const IrInstruction &instruction) noexcept {
    const vietvm::frontend::Token *first = firstTokenFor(sourceOwner, instruction);
    if (first == nullptr) return false;

    // A statement-leading keyword is dispatched before the generic callable
    // parser by the compatibility compiler (`in`, `dừng`, ...). It cannot be
    // treated as an ordinary direct call merely because semantic resolution
    // found a same-spelled function.
    if (first->kind == vietvm::frontend::TokenKind::Keyword ||
        !isCallableNamePiece(first->lexeme)) {
        return false;
    }

    bool sawFirst = false;
    for (const vietvm::frontend::Token &token : sourceOwner.tokens) {
        if (token.span.begin.offset < instruction.span.begin.offset ||
            token.span.end.offset > instruction.span.end.offset) {
            continue;
        }
        if (!sawFirst) sawFirst = true;
        if (token.lexeme == "(") return true;
        if (token.lexeme == ";" || token.lexeme == "," || token.lexeme == "=" ||
            token.lexeme == "{" || token.lexeme == "}" || token.lexeme == "[" ||
            token.lexeme == "]" || token.lexeme == ")" ||
            isOperator(token.lexeme)) {
            return false;
        }
        if (!isCallableNamePiece(token.lexeme)) return false;
    }
    return false;
}

bool containsCall(const IrProgram &program,
                  IrValueId id,
                  std::unordered_set<IrValueId> &visiting) {
    const IrValue *value = program.value(id);
    if (value == nullptr || !visiting.insert(id).second) return false;
    if (value->opcode == IrValueOpcode::Call ||
        value->opcode == IrValueOpcode::CallDynamic) {
        visiting.erase(id);
        return true;
    }
    for (IrValueId operand : value->operands) {
        if (containsCall(program, operand, visiting)) {
            visiting.erase(id);
            return true;
        }
    }
    visiting.erase(id);
    return false;
}

bool containsCallSplitterStringHazard(const IrProgram &program,
                                      IrValueId id,
                                      std::unordered_set<IrValueId> &visiting) {
    const IrValue *value = program.value(id);
    if (value == nullptr || !visiting.insert(id).second) return true;
    if (value->opcode == IrValueOpcode::ConstString &&
        (value->text.find(',') != std::string::npos ||
         value->text.find('(') != std::string::npos ||
         value->text.find(')') != std::string::npos)) {
        visiting.erase(id);
        return true;
    }
    for (IrValueId operand : value->operands) {
        if (containsCallSplitterStringHazard(program, operand, visiting)) {
            visiting.erase(id);
            return true;
        }
    }
    visiting.erase(id);
    return false;
}

bool containsCallSplitterHazard(const IrProgram &program,
                                const IrValue &call) {
    for (std::size_t index = 1; index < call.operands.size(); ++index) {
        std::unordered_set<IrValueId> visiting;
        if (containsCallSplitterStringHazard(
                program, call.operands[index], visiting)) {
            return true;
        }
    }
    return false;
}

bool containsLoopHeaderStringHazard(const IrProgram &program,
                                    IrValueId id,
                                    std::unordered_set<IrValueId> &visiting) {
    const IrValue *value = program.value(id);
    if (value == nullptr || !visiting.insert(id).second) return true;
    if (value->opcode == IrValueOpcode::ConstString &&
        (value->text.find(';') != std::string::npos ||
         value->text.find('(') != std::string::npos ||
         value->text.find(')') != std::string::npos)) {
        visiting.erase(id);
        return true;
    }
    for (IrValueId operand : value->operands) {
        if (containsLoopHeaderStringHazard(program, operand, visiting)) {
            visiting.erase(id);
            return true;
        }
    }
    visiting.erase(id);
    return false;
}

bool expressionConsumesStatement(const IrInstruction &sourceOwner,
                                 const IrInstruction &instruction,
                                 const IrValue &root) noexcept {
    if (root.span.begin.offset != instruction.span.begin.offset) return false;

    const vietvm::frontend::Token *lastExpressionToken = nullptr;
    for (const vietvm::frontend::Token &token : sourceOwner.tokens) {
        if (token.span.begin.offset < instruction.span.begin.offset ||
            token.span.end.offset > instruction.span.end.offset ||
            token.lexeme == ";") {
            continue;
        }
        lastExpressionToken = &token;
    }
    return lastExpressionToken != nullptr &&
           lastExpressionToken->span.end.offset == root.span.end.offset;
}

bool hasLegacyMapBounds(const IrInstruction &instruction,
                        const IrValue &value) noexcept {
    bool startsWithBrace = false;
    bool endsWithBrace = false;
    for (const vietvm::frontend::Token &token : instruction.tokens) {
        if (token.span.begin.offset == value.span.begin.offset) {
            startsWithBrace = token.lexeme == "{";
        }
        if (token.span.end.offset == value.span.end.offset) {
            endsWithBrace = token.lexeme == "}";
        }
    }
    return startsWithBrace && endsWithBrace;
}

bool isMapKey(const IrInstruction &instruction,
              const IrValue *value) noexcept {
    if (value == nullptr || !value->operands.empty()) return false;
    if (!isExactSourceToken(instruction, *value)) return false;
    if (value->opcode == IrValueOpcode::ConstString) {
        return isStringLiteral(value->text);
    }
    if (value->opcode == IrValueOpcode::LoadName) {
        // The legacy map parser accepts one identifier token here. In
        // particular, a contextual multi-word AST name is not a map key.
        return isVariable(value->text);
    }
    return false;
}

bool isMapValue(const IrInstruction &instruction,
                const IrValue *value) noexcept {
    if (value == nullptr || !value->operands.empty()) return false;
    if (!isExactSourceToken(instruction, *value)) return false;
    switch (value->opcode) {
        case IrValueOpcode::ConstInt: return isNumber(value->text);
        case IrValueOpcode::ConstFloat: return isFloat(value->text);
        case IrValueOpcode::ConstString: return isStringLiteral(value->text);
        case IrValueOpcode::ConstBool:
            return value->text == "đúng" || value->text == "sai";
        case IrValueOpcode::ConstNull: return value->text == "rỗng";
        default: return false;
    }
}

enum class ValueContext {
    Nested,
    ExpressionStatementRoot,
    OtherStatementRoot,
    DedicatedCallStatementRoot,
    SimpleAssignmentRhs,
};

struct SupportContext {
    std::unordered_set<int> functionSymbols;
    std::unordered_set<std::string> functionNames;
    std::unordered_set<int> methodSymbols;
    std::unordered_set<std::string> methodRawNames;
    std::unordered_map<int, std::size_t> methodDeclarationOffsets;
    std::unordered_map<std::string, std::size_t> methodNameDeclarationOffsets;
};

bool methodIsAllocatedBeforeUse(const SupportContext &context,
                                const IrValue &value) noexcept {
    if (value.symbolId >= 0 &&
        context.methodSymbols.find(value.symbolId) != context.methodSymbols.end()) {
        const auto declaration =
            context.methodDeclarationOffsets.find(value.symbolId);
        return declaration != context.methodDeclarationOffsets.end() &&
               declaration->second <= value.span.begin.offset;
    }
    const auto namedDeclaration =
        context.methodNameDeclarationOffsets.find(value.text);
    return namedDeclaration == context.methodNameDeclarationOffsets.end() ||
           namedDeclaration->second <= value.span.begin.offset;
}

bool supportsInstruction(const IrProgram &program,
                         const IrInstruction &instruction,
                         const IrInstruction &sourceOwner,
                         const SupportContext &context,
                         bool insideFunction,
                         bool insideBlock,
                         std::size_t switchDepth,
                         bool topLevel);

bool hasExactLambdaSourceShape(const IrInstruction &sourceOwner,
                               const IrValue &value,
                               const IrLambda &lambda) noexcept {
    if (value.span.begin.offset != lambda.span.begin.offset ||
        value.span.end.offset != lambda.span.end.offset) {
        // Grouping widens the expression span while the lambda payload keeps
        // its actual `hàm (...) { ... }` span. The compatibility lambda parser
        // only recognizes a slice beginning directly at `hàm`.
        return false;
    }

    bool startsWithLambda = false;
    bool endsWithBlock = false;
    for (const vietvm::frontend::Token &token : sourceOwner.tokens) {
        if (token.span.begin.offset == value.span.begin.offset) {
            startsWithLambda = token.lexeme == "hàm";
        }
        if (token.span.end.offset == value.span.end.offset) {
            endsWithBlock = token.lexeme == "}";
        }
    }
    return startsWithLambda && endsWithBlock;
}

bool containsLambdaValue(const IrProgram &program,
                         IrValueId id,
                         std::unordered_set<IrValueId> &visited);

bool containsLambdaInstruction(const IrProgram &program,
                               const IrInstruction &instruction,
                               std::unordered_set<IrValueId> &visited) {
    for (const IrParameter &parameter : instruction.parameters) {
        if (parameter.defaultValue != kInvalidIrValueId &&
            containsLambdaValue(program, parameter.defaultValue, visited)) {
            return true;
        }
    }
    for (IrValueId root : instruction.expressionRoots) {
        if (containsLambdaValue(program, root, visited)) return true;
    }
    for (const IrInstruction &child : instruction.children) {
        if (containsLambdaInstruction(program, child, visited)) return true;
    }
    return false;
}

bool containsLambdaValue(const IrProgram &program,
                         IrValueId id,
                         std::unordered_set<IrValueId> &visited) {
    const IrValue *value = program.value(id);
    if (value == nullptr || !visited.insert(id).second) return false;
    if (value->opcode == IrValueOpcode::Lambda) return true;
    for (IrValueId operand : value->operands) {
        if (containsLambdaValue(program, operand, visited)) return true;
    }
    return false;
}

bool supportsValue(const IrProgram &program,
                   const IrInstruction &sourceOwner,
                   const SupportContext &context,
                   IrValueId id,
                   ValueContext valueContext,
                   std::unordered_set<IrValueId> &visiting) {
    const IrValue *value = program.value(id);
    if (value == nullptr || !visiting.insert(id).second) return false;

    bool supported = false;
    switch (value->opcode) {
        case IrValueOpcode::ConstInt:
        case IrValueOpcode::ConstFloat:
        case IrValueOpcode::ConstString:
        case IrValueOpcode::ConstBool:
        case IrValueOpcode::ConstNull:
            supported = value->operands.empty();
            break;

        case IrValueOpcode::LoadName:
            supported = value->operands.empty() &&
                        methodIsAllocatedBeforeUse(context, *value);
            break;

        case IrValueOpcode::MapLiteral:
            // Legacy compileExpr recognizes a map only when it occupies the
            // complete expression slice or the complete RHS of a simple '='.
            // It does not parse a map nested under unary/binary/compound ops.
            supported = valueContext != ValueContext::Nested &&
                        valueContext != ValueContext::DedicatedCallStatementRoot &&
                        hasLegacyMapBounds(sourceOwner, *value) &&
                        value->operands.size() % 2 == 0;
            for (std::size_t index = 0; supported && index < value->operands.size(); index += 2) {
                supported = isMapKey(
                                sourceOwner,
                                program.value(value->operands[index])) &&
                            isMapValue(
                                sourceOwner,
                                program.value(value->operands[index + 1]));
            }
            break;

        case IrValueOpcode::Unary:
            supported = value->operands.size() == 1 &&
                        value->text == "!" &&
                        supportsValue(program, sourceOwner, context,
                                      value->operands.front(),
                                      ValueContext::Nested, visiting);
            break;

        case IrValueOpcode::Binary:
            supported = value->operands.size() == 2 && isBinaryOperator(value->text) &&
                        supportsValue(program, sourceOwner, context,
                                      value->operands[0],
                                      ValueContext::Nested, visiting) &&
                        supportsValue(program, sourceOwner, context,
                                      value->operands[1],
                                      ValueContext::Nested, visiting);
            break;

        case IrValueOpcode::StoreName: {
            if (valueContext != ValueContext::ExpressionStatementRoot ||
                value->operands.empty()) {
                break;
            }
            const IrValue *target = program.value(value->operands.front());
            if (target == nullptr || target->opcode != IrValueOpcode::LoadName ||
                target->text.empty() || !isExactSourceName(sourceOwner, *target) ||
                value->span.begin.offset != target->span.begin.offset ||
                !methodIsAllocatedBeforeUse(context, *target)) {
                break;
            }
            if (value->text == "++" || value->text == "--") {
                supported = value->operands.size() == 1 &&
                            endsWithSourceToken(sourceOwner, *value, value->text);
                break;
            }
            supported = value->operands.size() == 2 &&
                        (value->text == "=" || isCompoundAssignment(value->text)) &&
                        supportsValue(
                            program, sourceOwner, context, value->operands[1],
                            value->text == "=" ? ValueContext::SimpleAssignmentRhs
                                               : ValueContext::Nested,
                            visiting);
            break;
        }

        case IrValueOpcode::Call: {
            if (value->explicitCall &&
                valueContext != ValueContext::DedicatedCallStatementRoot) {
                break;
            }
            if (value->operands.empty() || value->symbolId < 0 ||
                context.functionSymbols.find(value->symbolId) ==
                    context.functionSymbols.end()) {
                break;
            }
            const IrValue *callee = program.value(value->operands.front());
            supported = callee != nullptr &&
                        callee->opcode == IrValueOpcode::LoadName &&
                        callee->text == value->text &&
                        isExactSourceName(sourceOwner, *callee) &&
                        methodIsAllocatedBeforeUse(context, *value) &&
                        methodIsAllocatedBeforeUse(context, *callee);
            for (std::size_t index = 1;
                 supported && index < value->operands.size(); ++index) {
                supported = supportsValue(
                    program, sourceOwner, context, value->operands[index],
                    ValueContext::Nested, visiting);
            }
            break;
        }

        case IrValueOpcode::CallDynamic: {
            if (value->explicitCall &&
                valueContext != ValueContext::DedicatedCallStatementRoot) {
                break;
            }
            if (value->callTarget != CallTargetKind::DynamicName &&
                value->callTarget != CallTargetKind::Native &&
                value->callTarget != CallTargetKind::IndirectValue) {
                break;
            }
            if (value->operands.empty() || value->text.empty()) break;
            const IrValue *callee = program.value(value->operands.front());
            supported = callee != nullptr &&
                        callee->opcode == IrValueOpcode::LoadName &&
                        callee->text == value->text &&
                        isExactSourceName(sourceOwner, *callee) &&
                        methodIsAllocatedBeforeUse(context, *value) &&
                        methodIsAllocatedBeforeUse(context, *callee);
            for (std::size_t index = 1;
                 supported && index < value->operands.size(); ++index) {
                supported = supportsValue(
                    program, sourceOwner, context, value->operands[index],
                    ValueContext::Nested, visiting);
            }
            break;
        }

        case IrValueOpcode::Lambda: {
            if (valueContext != ValueContext::SimpleAssignmentRhs ||
                value->lambdaId == kInvalidIrLambdaId) {
                break;
            }
            const IrLambda *lambda = program.lambda(value->lambdaId);
            if (lambda == nullptr || lambda->ownerValue != value->id ||
                lambda->sourceExprId != value->sourceExprId ||
                lambda->body.opcode != IrOpcode::Block ||
                !lambda->captures.empty() ||
                !hasExactLambdaSourceShape(sourceOwner, *value, *lambda)) {
                break;
            }

            supported = true;
            for (const IrParameter &parameter : lambda->parameters) {
                if (parameter.name.empty() ||
                    context.functionNames.find(parameter.name) !=
                        context.functionNames.end()) {
                    supported = false;
                    break;
                }
                if (!parameter.hasDefault) {
                    if (parameter.defaultValue != kInvalidIrValueId) {
                        supported = false;
                    }
                    continue;
                }
                const IrValue *defaultValue =
                    program.value(parameter.defaultValue);
                if (!isMapValue(sourceOwner, defaultValue) ||
                    (defaultValue->opcode == IrValueOpcode::ConstString &&
                     defaultValue->text.find(',') != std::string::npos)) {
                    supported = false;
                    break;
                }
            }
            if (!supported) break;

            std::unordered_set<IrValueId> nestedSearch;
            for (const IrParameter &parameter : lambda->parameters) {
                if (parameter.defaultValue != kInvalidIrValueId &&
                    containsLambdaValue(
                        program, parameter.defaultValue, nestedSearch)) {
                    supported = false;
                    break;
                }
            }
            if (supported && containsLambdaInstruction(
                                 program, lambda->body, nestedSearch)) {
                supported = false;
            }
            if (supported) {
                supported = supportsInstruction(
                    program, lambda->body, sourceOwner, context,
                    true, true, 0, false);
            }
            break;
        }

        case IrValueOpcode::LegacyRegion:
            supported = false;
            break;
    }

    visiting.erase(id);
    return supported;
}

bool classMethodCallsAreContextStableValue(
    const IrProgram &program,
    IrValueId id,
    const SupportContext &context,
    std::unordered_set<IrValueId> &visited);

bool classMethodCallsAreContextStableInstruction(
    const IrProgram &program,
    const IrInstruction &instruction,
    const SupportContext &context,
    std::unordered_set<IrValueId> &visited) {
    for (const IrParameter &parameter : instruction.parameters) {
        if (parameter.defaultValue != kInvalidIrValueId &&
            !classMethodCallsAreContextStableValue(
                program, parameter.defaultValue, context, visited)) {
            return false;
        }
    }
    for (IrValueId root : instruction.expressionRoots) {
        if (!classMethodCallsAreContextStableValue(
                program, root, context, visited)) {
            return false;
        }
    }
    for (const IrInstruction &child : instruction.children) {
        if (!classMethodCallsAreContextStableInstruction(
                program, child, context, visited)) {
            return false;
        }
    }
    return true;
}

bool classMethodCallsAreContextStableValue(
    const IrProgram &program,
    IrValueId id,
    const SupportContext &context,
    std::unordered_set<IrValueId> &visited) {
    const IrValue *value = program.value(id);
    if (value == nullptr) return false;
    if (!visited.insert(id).second) return true;

    if (value->opcode == IrValueOpcode::LoadName &&
        value->text.find('.') == std::string::npos &&
        context.methodRawNames.find(value->text) !=
            context.methodRawNames.end() &&
        (value->symbolId < 0 ||
         context.methodSymbols.find(value->symbolId) ==
             context.methodSymbols.end())) {
        // In a class context the legacy expression emitter resolves a raw
        // method spelling before a lexical parameter/local/catch of the same
        // name. Semantic lookup intentionally does the opposite. Keep this
        // compatibility collision on the bridge until symbol semantics own
        // bytecode behavior outright.
        return false;
    }

    if ((value->opcode == IrValueOpcode::Call ||
         value->opcode == IrValueOpcode::CallDynamic) &&
        value->text.find('.') == std::string::npos) {
        // The compatibility resolver runs after argument emission and treats
        // an already-present `Class.name` StringPool entry as evidence for a
        // class-qualified call. An argument can therefore change dispatch of
        // an otherwise global/dynamic/indirect unqualified call. Only a
        // semantically resolved method has a stable unqualified meaning here.
        if (value->opcode != IrValueOpcode::Call || value->symbolId < 0 ||
            context.methodSymbols.find(value->symbolId) ==
                context.methodSymbols.end()) {
            return false;
        }
    }

    for (IrValueId operand : value->operands) {
        if (!classMethodCallsAreContextStableValue(
                program, operand, context, visited)) {
            return false;
        }
    }
    if (value->opcode == IrValueOpcode::Lambda) {
        const IrLambda *lambda = program.lambda(value->lambdaId);
        if (lambda == nullptr || lambda->ownerValue != id) return false;
        for (const IrParameter &parameter : lambda->parameters) {
            if (parameter.defaultValue != kInvalidIrValueId &&
                !classMethodCallsAreContextStableValue(
                    program, parameter.defaultValue, context, visited)) {
                return false;
            }
        }
        if (!classMethodCallsAreContextStableInstruction(
                program, lambda->body, context, visited)) {
            return false;
        }
    }
    return true;
}

bool supportsFunction(const IrProgram &program,
                      const IrInstruction &instruction,
                      const IrInstruction &sourceOwner,
                      const SupportContext &context,
                      bool classMethod) {
    if (instruction.symbolId < 0 || instruction.declarationName.empty() ||
        context.functionSymbols.find(instruction.symbolId) ==
            context.functionSymbols.end() ||
        !instruction.expressionRoots.empty() || instruction.children.size() != 1 ||
        instruction.children.front().opcode != IrOpcode::Block) {
        return false;
    }

    for (const IrParameter &parameter : instruction.parameters) {
        if (parameter.name.empty() ||
            context.functionNames.find(parameter.name) != context.functionNames.end()) {
            // The legacy expression emitter gives a same-spelled function
            // priority over a parameter value. Keep this compatibility edge
            // on the bridge until symbol-based codegen becomes authoritative.
            return false;
        }
        if (!parameter.hasDefault) {
            if (parameter.defaultValue != kInvalidIrValueId) return false;
            continue;
        }
        const IrValue *defaultValue = program.value(parameter.defaultValue);
        if (!isMapValue(sourceOwner, defaultValue)) return false;
        if (defaultValue->opcode == IrValueOpcode::ConstString &&
            defaultValue->text.find(',') != std::string::npos) {
            // The legacy function header parser splits parameters at every
            // comma, including commas inside a string default. Keep that
            // diagnostic contract until the old header parser is retired.
            return false;
        }
    }

    if (classMethod) {
        std::unordered_set<IrValueId> visited;
        if (!classMethodCallsAreContextStableInstruction(
                program, instruction.children.front(), context, visited)) {
            return false;
        }
    }

    return supportsInstruction(program, instruction.children.front(), sourceOwner,
                               context, true, true, 0, false);
}

bool supportsInstruction(const IrProgram &program,
                         const IrInstruction &instruction,
                         const IrInstruction &sourceOwner,
                         const SupportContext &context,
                         bool insideFunction,
                         bool insideBlock,
                         std::size_t switchDepth,
                         bool topLevel) {
    if (instruction.legacyRegion) return false;

    if (instruction.opcode == IrOpcode::NoOp) {
        return instruction.expressionRoots.empty() && instruction.children.empty();
    }
    if (instruction.opcode == IrOpcode::DefineFunction) {
        return topLevel && supportsFunction(
            program, instruction, sourceOwner, context, false);
    }
    if (instruction.opcode == IrOpcode::DefineClass) {
        if (!topLevel ||
            instruction.classForm !=
                vietvm::frontend::AstClassForm::MethodBlock ||
            instruction.declarationName.empty() ||
            !instruction.expressionRoots.empty() ||
            instruction.children.size() != 1 ||
            instruction.children.front().opcode != IrOpcode::Block ||
            instruction.children.front().legacyRegion ||
            !instruction.children.front().expressionRoots.empty()) {
            return false;
        }
        for (const IrInstruction &member :
             instruction.children.front().children) {
            if (member.opcode == IrOpcode::NoOp) {
                if (member.legacyRegion || !member.expressionRoots.empty() ||
                    !member.children.empty()) {
                    return false;
                }
                continue;
            }
            if (member.opcode != IrOpcode::DefineFunction ||
                !supportsFunction(program, member, sourceOwner, context, true)) {
                return false;
            }
        }
        return true;
    }
    if (instruction.opcode == IrOpcode::Block) {
        if (!insideBlock || !instruction.expressionRoots.empty()) return false;
        for (const IrInstruction &child : instruction.children) {
            if (!supportsInstruction(program, child, sourceOwner, context,
                                     insideFunction, true, switchDepth, false)) {
                return false;
            }
        }
        return true;
    }
    if (instruction.opcode == IrOpcode::Conditional) {
        const bool hasElse = instruction.conditionalForm ==
            vietvm::frontend::AstConditionalForm::IfElseBlocks;
        if ((instruction.conditionalForm !=
                 vietvm::frontend::AstConditionalForm::IfBlock &&
             !hasElse) ||
            instruction.expressionRoots.size() != 1 ||
            instruction.children.size() != (hasElse ? 2u : 1u)) {
            return false;
        }

        const IrValue *condition = program.value(
            instruction.expressionRoots.front());
        if (condition == nullptr ||
            condition->opcode == IrValueOpcode::StoreName ||
            condition->opcode == IrValueOpcode::MapLiteral) {
            return false;
        }
        std::unordered_set<IrValueId> visiting;
        if (!supportsValue(program, sourceOwner, context, condition->id,
                           ValueContext::Nested, visiting)) {
            return false;
        }
        for (const IrInstruction &branch : instruction.children) {
            if (branch.opcode != IrOpcode::Block ||
                !supportsInstruction(program, branch, sourceOwner, context,
                                     insideFunction, true, switchDepth, false)) {
                return false;
            }
        }
        return true;
    }
    if (instruction.opcode == IrOpcode::Loop) {
        if (instruction.loopForm != vietvm::frontend::AstLoopForm::ForBlock ||
            instruction.expressionRoots.size() != 3 ||
            instruction.children.size() != 1 ||
            instruction.children.front().opcode != IrOpcode::Block) {
            return false;
        }

        const IrValue *init = program.value(instruction.expressionRoots[0]);
        const IrValue *condition = program.value(instruction.expressionRoots[1]);
        const IrValue *update = program.value(instruction.expressionRoots[2]);
        if (init == nullptr || condition == nullptr || update == nullptr ||
            init->opcode != IrValueOpcode::StoreName || init->text != "=" ||
            init->operands.size() != 2 ||
            update->opcode != IrValueOpcode::StoreName ||
            condition->opcode == IrValueOpcode::StoreName ||
            condition->opcode == IrValueOpcode::MapLiteral) {
            return false;
        }
        for (IrValueId root : instruction.expressionRoots) {
            std::unordered_set<IrValueId> hazardSearch;
            if (containsLoopHeaderStringHazard(program, root, hazardSearch)) {
                return false;
            }
        }

        std::unordered_set<IrValueId> initVisit;
        std::unordered_set<IrValueId> conditionVisit;
        std::unordered_set<IrValueId> updateVisit;
        if (!supportsValue(program, sourceOwner, context, init->id,
                           ValueContext::ExpressionStatementRoot, initVisit) ||
            !supportsValue(program, sourceOwner, context, condition->id,
                           ValueContext::Nested, conditionVisit) ||
            !supportsValue(program, sourceOwner, context, update->id,
                           ValueContext::ExpressionStatementRoot, updateVisit)) {
            return false;
        }
        return supportsInstruction(
            program, instruction.children.front(), sourceOwner, context,
            insideFunction, true, switchDepth, false);
    }
    if (instruction.opcode == IrOpcode::Switch) {
        if (instruction.switchForm !=
                vietvm::frontend::AstSwitchForm::Structured ||
            instruction.expressionRoots.empty() ||
            instruction.switchArms.empty() ||
            instruction.switchArms.size() != instruction.children.size()) {
            return false;
        }

        const IrValue *selector = program.value(
            instruction.expressionRoots.front());
        if (selector == nullptr || selector->opcode == IrValueOpcode::StoreName ||
            selector->opcode == IrValueOpcode::MapLiteral) {
            return false;
        }
        std::unordered_set<IrValueId> selectorVisit;
        if (!supportsValue(program, sourceOwner, context, selector->id,
                           ValueContext::Nested, selectorVisit)) {
            return false;
        }

        std::vector<bool> ownedBodies(instruction.children.size(), false);
        std::size_t caseCount = 0;
        for (const IrSwitchArm &arm : instruction.switchArms) {
            if (arm.bodyChildIndex >= instruction.children.size() ||
                ownedBodies[arm.bodyChildIndex]) {
                return false;
            }
            ownedBodies[arm.bodyChildIndex] = true;

            if (arm.kind == vietvm::frontend::AstSwitchArmKind::Default) {
                if (arm.label != kInvalidIrValueId) return false;
            } else {
                ++caseCount;
                if (!arm.prefixedByCase || arm.label == kInvalidIrValueId) {
                    return false;
                }
                const IrValue *label = program.value(arm.label);
                if (label == nullptr || !label->operands.empty() ||
                    !isExactSourceToken(sourceOwner, *label)) {
                    return false;
                }
                if (label->opcode == IrValueOpcode::ConstInt) {
                    if (!isNumber(label->text)) return false;
                    try {
                        (void)std::stoi(label->text);
                    } catch (...) {
                        // Let the compatibility compiler retain its exact
                        // malformed/overflow diagnostic rather than replacing
                        // it with a direct-emitter exception.
                        return false;
                    }
                } else if (label->opcode == IrValueOpcode::ConstString) {
                    if (!isStringLiteral(label->text)) return false;
                } else if (label->opcode == IrValueOpcode::LoadName) {
                    if (!isVariable(label->text) ||
                        !methodIsAllocatedBeforeUse(context, *label)) {
                        return false;
                    }
                } else {
                    return false;
                }
            }

            const IrInstruction &body = instruction.children[arm.bodyChildIndex];
            if (body.opcode != IrOpcode::Block ||
                !supportsInstruction(program, body, sourceOwner, context,
                                     insideFunction, true, switchDepth + 1,
                                     false)) {
                return false;
            }
        }
        return caseCount + 1 == instruction.expressionRoots.size();
    }
    if (instruction.opcode == IrOpcode::Break) {
        return switchDepth > 0 && instruction.expressionRoots.empty() &&
            instruction.children.empty() &&
            (hasExactStatementLexemes(sourceOwner, instruction, {"thoát"}) ||
             hasExactStatementLexemes(sourceOwner, instruction,
                                      {"thoát", ";"}));
    }
    if (instruction.opcode == IrOpcode::Try) {
        if (instruction.tryForm !=
                vietvm::frontend::AstTryForm::TryCatchBlocks ||
            !instruction.expressionRoots.empty() ||
            instruction.children.size() != 2 ||
            instruction.children[0].opcode != IrOpcode::Block ||
            instruction.children[1].opcode != IrOpcode::Block) {
            return false;
        }
        if (!instruction.catchVariable.empty() &&
            (instruction.catchSymbolId < 0 ||
             context.functionNames.find(instruction.catchVariable) !=
                 context.functionNames.end())) {
            return false;
        }
        return supportsInstruction(program, instruction.children[0], sourceOwner,
                                   context, insideFunction, true, switchDepth,
                                   false) &&
               supportsInstruction(program, instruction.children[1], sourceOwner,
                                   context, insideFunction, true, switchDepth,
                                   false);
    }
    if (instruction.opcode == IrOpcode::Throw) {
        if (!insideBlock || !instruction.children.empty() ||
            instruction.expressionRoots.size() > 1 ||
            !endsWithStatementToken(sourceOwner, instruction, ";")) {
            return false;
        }
        if (instruction.expressionRoots.empty()) {
            return hasExactStatementLexemes(
                sourceOwner, instruction, {"ném", ";"});
        }
        const IrValue *root = program.value(instruction.expressionRoots.front());
        if (root == nullptr || root->opcode == IrValueOpcode::StoreName) return false;
        std::unordered_set<IrValueId> visiting;
        return supportsValue(program, sourceOwner, context, root->id,
                             ValueContext::OtherStatementRoot, visiting);
    }
    if (instruction.opcode == IrOpcode::Continue) {
        return instruction.expressionRoots.empty() && instruction.children.empty() &&
            (hasExactStatementLexemes(
                 sourceOwner, instruction, {"bỏ qua"}) ||
             hasExactStatementLexemes(
                 sourceOwner, instruction, {"bỏ qua", ";"}));
    }
    if (instruction.opcode == IrOpcode::Return) {
        if (!insideFunction || !instruction.children.empty() ||
            instruction.expressionRoots.size() > 1 ||
            !endsWithStatementToken(sourceOwner, instruction, ";")) {
            return false;
        }
        if (instruction.expressionRoots.empty()) return true;
        const IrValue *root = program.value(instruction.expressionRoots.front());
        if (root == nullptr || root->opcode == IrValueOpcode::StoreName) return false;
        std::unordered_set<IrValueId> visiting;
        return supportsValue(program, sourceOwner, context, root->id,
                             ValueContext::OtherStatementRoot, visiting);
    }
    if (instruction.opcode != IrOpcode::Print &&
        instruction.opcode != IrOpcode::Statement) {
        return false;
    }
    if (!instruction.children.empty() || instruction.expressionRoots.size() != 1) {
        return false;
    }

    std::unordered_set<IrValueId> visiting;
    const bool assignmentAllowed = instruction.opcode == IrOpcode::Statement;
    const IrValue *root = program.value(instruction.expressionRoots.front());
    if (root == nullptr) return false;
    if (!assignmentAllowed && root->opcode == IrValueOpcode::StoreName) return false;

    const vietvm::frontend::Token *statementFirst =
        firstTokenFor(sourceOwner, instruction);
    const bool rootIsCall = root->opcode == IrValueOpcode::Call ||
        root->opcode == IrValueOpcode::CallDynamic;
    const bool explicitStatementCall = rootIsCall &&
        root->explicitCall && statementFirst != nullptr &&
        statementFirst->lexeme == "gọi";
    const bool dedicatedCall = instruction.opcode == IrOpcode::Statement &&
        (explicitStatementCall ||
         startsWithDedicatedCallSyntax(sourceOwner, instruction));
    if (dedicatedCall &&
         (!rootIsCall ||
         !expressionConsumesStatement(sourceOwner, instruction, *root) ||
         containsCallSplitterHazard(program, *root))) {
        // Legacy compileStatement dispatches a leading `name(...)` before the
        // generic expression parser. Text following ')' is therefore not part
        // of that call statement and must not silently gain new semantics.
        return false;
    }
    if (instruction.opcode == IrOpcode::Statement && !dedicatedCall) {
        if (rootIsCall) return false;
        if (root->opcode != IrValueOpcode::StoreName) {
            std::unordered_set<IrValueId> callSearch;
            if (containsCall(program, root->id, callSearch)) return false;
        }
    }
    if (insideBlock && !dedicatedCall &&
        !endsWithStatementToken(sourceOwner, instruction, ";")) {
        // Generic expression, print, and return handlers scan until ';'. A
        // closing function brace is not a terminator in the compatibility
        // grammar. Dedicated call handlers are the only semicolon-optional
        // statements in this direct cohort.
        return false;
    }

    ValueContext valueContext = instruction.opcode == IrOpcode::Statement
        ? ValueContext::ExpressionStatementRoot
        : ValueContext::OtherStatementRoot;
    if (dedicatedCall) valueContext = ValueContext::DedicatedCallStatementRoot;
    return supportsValue(program, sourceOwner, context, root->id,
                         valueContext, visiting);
}

Opcode binaryOpcode(const std::string &op) {
    if (op == "+") return OP_CONG;
    if (op == "-") return OP_TRU;
    if (op == "*") return OP_NHAN;
    if (op == "/") return OP_CHIA;
    if (op == "%") return OP_MODULO;
    if (op == "==") return OP_SO_SANH_BANG;
    if (op == "!=") return OP_KHAC_BANG;
    if (op == "<") return OP_NHO_HON;
    if (op == ">") return OP_LON_HON;
    if (op == "<=") return OP_NHO_HON_HOAC_BANG;
    if (op == ">=") return OP_LON_HON_HOAC_BANG;
    if (op == "&&") return OP_Logic_VA;
    if (op == "||") return OP_Logic_HOAC;
    throw std::logic_error("unsupported direct IR binary operator");
}

Opcode compoundOpcode(const std::string &op) {
    if (op == "+=") return OP_CONG;
    if (op == "-=") return OP_TRU;
    if (op == "*=") return OP_NHAN;
    if (op == "/=") return OP_CHIA;
    if (op == "%=") return OP_MODULO;
    throw std::logic_error("unsupported direct IR compound assignment");
}

std::string unquote(const std::string &text) {
    if (text.size() >= 2 &&
        ((text.front() == '"' && text.back() == '"') ||
         (text.front() == '\'' && text.back() == '\''))) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

std::string encodeMapField(const std::string &text) {
    std::string encoded;
    encoded.reserve(text.size() + 8);
    for (unsigned char byte : text) {
        if (byte == '\\' || byte == '\n' || byte == '\r' || byte == '\t' ||
            byte == '\x1e' || byte == '\x1f') {
            encoded.push_back('\\');
            if (byte == '\n') encoded.push_back('n');
            else if (byte == '\r') encoded.push_back('r');
            else if (byte == '\t') encoded.push_back('t');
            else if (byte == '\x1e') encoded.push_back('e');
            else if (byte == '\x1f') encoded.push_back('f');
            else encoded.push_back('\\');
        } else {
            encoded.push_back(static_cast<char>(byte));
        }
    }
    return encoded;
}

std::string encodeMapLiteral(const IrProgram &program, const IrValue &map) {
    constexpr char recordSeparator = '\x1e';
    constexpr char fieldSeparator = '\x1f';
    std::ostringstream encoded;
    for (std::size_t index = 0; index < map.operands.size(); index += 2) {
        const IrValue &key = *program.value(map.operands[index]);
        const IrValue &value = *program.value(map.operands[index + 1]);
        if (index != 0) encoded << recordSeparator;
        encoded << encodeMapField(key.opcode == IrValueOpcode::ConstString
                                      ? stripQuotes(key.text)
                                      : key.text)
                << fieldSeparator;
        switch (value.opcode) {
            case IrValueOpcode::ConstInt:
            case IrValueOpcode::ConstBool:
                encoded << 'i' << fieldSeparator;
                if (value.opcode == IrValueOpcode::ConstBool) {
                    encoded << (value.text == "đúng" ? '1' : '0');
                } else {
                    encoded << encodeMapField(value.text);
                }
                break;
            case IrValueOpcode::ConstFloat:
                encoded << 'd' << fieldSeparator << encodeMapField(value.text);
                break;
            case IrValueOpcode::ConstString:
                // Unlike ordinary string expressions, the legacy map encoder
                // decodes source escapes before applying its RS/FS escaping.
                encoded << 's' << fieldSeparator
                        << encodeMapField(stripQuotes(value.text));
                break;
            case IrValueOpcode::ConstNull:
                encoded << 'n' << fieldSeparator;
                break;
            default:
                throw std::logic_error("unsupported direct IR map value");
        }
    }
    return encoded.str();
}

std::string encodeDefaultValue(const IrValue &value) {
    switch (value.opcode) {
        case IrValueOpcode::ConstInt: return "i:" + value.text;
        case IrValueOpcode::ConstFloat: return "d:" + value.text;
        case IrValueOpcode::ConstString: return "s:" + stripQuotes(value.text);
        case IrValueOpcode::ConstBool:
            return value.text == "đúng" ? "i:1" : "i:0";
        case IrValueOpcode::ConstNull: return "n:";
        default:
            throw std::logic_error("unsupported direct IR default parameter value");
    }
}

struct Emitter {
    const IrProgram &program;
    std::vector<Instruction> bytecode;
    std::unordered_map<std::string, int> slots;
    std::unordered_map<int, int> functionIdsBySymbol;
    std::unordered_map<std::string, int> functionIdsByName;
    std::unordered_map<int, int> functionNameIndices;
    std::unordered_map<IrLambdaId, int> lambdaIds;
    int nextSlot = 0;

    explicit Emitter(const IrProgram &ir) : program(ir) {}

    void allocateFunction(const IrInstruction &instruction) {
        if (functionIdsBySymbol.find(instruction.symbolId) !=
            functionIdsBySymbol.end()) {
            return;
        }
        const int functionId = hamMap::allocHamId();
        const int nameIndex = StringPool::storeString(instruction.declarationName);
        functionIdsBySymbol.emplace(instruction.symbolId, functionId);
        functionIdsByName.emplace(instruction.declarationName, functionId);
        functionNameIndices.emplace(instruction.symbolId, nameIndex);
        slots.emplace(instruction.declarationName, functionId);
        hamMap::hamBytecodeMap[functionId] = {};
        hamMap::setHamNameIndex(functionId, nameIndex);
        if (functionId >= nextSlot) nextSlot = functionId + 1;
    }

    void predeclareFunctions() {
        for (const IrInstruction &instruction : program.instructions) {
            if (instruction.opcode == IrOpcode::DefineFunction) {
                allocateFunction(instruction);
            }
        }
    }

    int slotFor(const std::string &name) {
        const auto found = slots.find(name);
        if (found != slots.end()) return found->second;
        const int slot = nextSlot++;
        slots.emplace(name, slot);
        return slot;
    }

    void emitValue(IrValueId id, std::vector<Instruction> &output) {
        const IrValue *value = program.value(id);
        if (value == nullptr) throw std::logic_error("invalid direct IR value id");

        switch (value->opcode) {
            case IrValueOpcode::ConstInt:
                try {
                    output.push_back({OP_BIEN_SO, std::stoi(value->text), 0, 0});
                } catch (...) {
                    throw std::runtime_error(vietvm::messages::formatMessage(
                        vietvm::messages::kInternalNumberParseMismatch, {value->text}));
                }
                return;
            case IrValueOpcode::ConstFloat: {
                const int index = StringPool::storeString(value->text);
                output.push_back({OP_BIEN_SO_FLOAT, 0, index, 0});
                return;
            }
            case IrValueOpcode::ConstString: {
                const int index = StringPool::storeString(unquote(value->text));
                output.push_back({OP_CHUOI, 0, index, 0});
                return;
            }
            case IrValueOpcode::ConstBool:
                output.push_back({OP_BIEN_SO, value->text == "đúng" ? 1 : 0, 0, 0});
                return;
            case IrValueOpcode::ConstNull:
                output.push_back({OP_RONG_GIA_TRI, 0, 0, 0});
                return;
            case IrValueOpcode::MapLiteral: {
                const int index = StringPool::storeString(encodeMapLiteral(program, *value));
                output.push_back({OP_MAP_LITERAL, 0, index, 0});
                return;
            }
            case IrValueOpcode::LoadName: {
                const auto function = functionIdsBySymbol.find(value->symbolId);
                if (function != functionIdsBySymbol.end()) {
                    output.push_back({OP_BIEN_SO, function->second, 0, 0});
                    return;
                }
                const int slot = slotFor(value->text);
                output.push_back({OP_TEN_BIEN_GIA_TRI, 0, slot, 0});
                return;
            }
            case IrValueOpcode::Unary:
                emitValue(value->operands.front(), output);
                output.push_back({OP_PHU_DINH, 0, 0, 0});
                return;
            case IrValueOpcode::Binary:
                emitValue(value->operands[0], output);
                emitValue(value->operands[1], output);
                output.push_back({binaryOpcode(value->text), 0, 0, 0});
                return;
            case IrValueOpcode::StoreName: {
                const IrValue *target = program.value(value->operands.front());
                if (target == nullptr) throw std::logic_error("invalid direct IR store target");

                // The legacy compiler assigns the target slot before compiling
                // the RHS.  Preserve that ordering even though the target ID is
                // pushed after the value.
                const int slot = slotFor(target->text);
                if (value->text == "++" || value->text == "--") {
                    output.push_back({OP_TEN_BIEN_ID, 0, slot, 0});
                    output.push_back({value->text == "++" ? OP_CONG_MOT : OP_TRU_MOT,
                                      0, 0, 0});
                    return;
                }
                if (isCompoundAssignment(value->text)) {
                    output.push_back({OP_TEN_BIEN_GIA_TRI, 0, slot, 0});
                    emitValue(value->operands[1], output);
                    output.push_back({compoundOpcode(value->text), 0, 0, 0});
                } else {
                    emitValue(value->operands[1], output);
                }
                output.push_back({OP_TEN_BIEN_ID, 0, slot, 0});
                output.push_back({OP_GAN, 0, 0, 0});
                return;
            }
            case IrValueOpcode::Call: {
                const auto function = functionIdsBySymbol.find(value->symbolId);
                if (function == functionIdsBySymbol.end()) {
                    throw std::logic_error("direct IR call has no VM function mapping");
                }
                for (std::size_t index = 1; index < value->operands.size(); ++index) {
                    emitValue(value->operands[index], output);
                }
                int target = function->second;
                if (value->explicitCall) {
                    const auto code = hamMap::hamBytecodeMap.find(function->second);
                    if (code == hamMap::hamBytecodeMap.end() || code->second.empty()) {
                        const auto name = functionNameIndices.find(value->symbolId);
                        if (name == functionNameIndices.end()) {
                            throw std::logic_error(
                                "direct IR explicit call has no function name mapping");
                        }
                        target = -(name->second + 1);
                    }
                }
                output.push_back({OP_GOI,
                                  static_cast<int>(value->operands.size() - 1),
                                  target,
                                  0});
                return;
            }
            case IrValueOpcode::CallDynamic: {
                for (std::size_t index = 1; index < value->operands.size(); ++index) {
                    emitValue(value->operands[index], output);
                }
                const int argumentCount =
                    static_cast<int>(value->operands.size() - 1);

                if (!value->explicitCall) {
                    const auto function = functionIdsByName.find(value->text);
                    if (function != functionIdsByName.end()) {
                        output.push_back(
                            {OP_GOI, argumentCount, function->second, 0});
                        return;
                    }
                    const auto variable = slots.find(value->text);
                    if (variable != slots.end()) {
                        output.push_back(
                            {OP_TEN_BIEN_GIA_TRI, 0, variable->second, 0});
                        output.push_back(
                            {OP_GOI_GIAN_TIEP, argumentCount, 0, 0});
                        return;
                    }
                }

                const int nameIndex = StringPool::storeString(value->text);
                output.push_back(
                    {OP_GOI, argumentCount, -(nameIndex + 1), 0});
                return;
            }
            case IrValueOpcode::Lambda: {
                const IrLambda *lambda = program.lambda(value->lambdaId);
                if (lambda == nullptr || lambda->ownerValue != value->id) {
                    throw std::logic_error(
                        "direct IR lambda has no structured payload");
                }

                const auto existing = lambdaIds.find(value->lambdaId);
                if (existing != lambdaIds.end()) {
                    output.push_back({OP_BIEN_SO, existing->second, 0, 0});
                    return;
                }

                // Anonymous functions are allocated exactly when their value
                // is emitted. They deliberately have no OP_HAM declaration,
                // no name mapping and no StringPool name entry.
                const int functionId = hamMap::allocHamId();
                lambdaIds.emplace(value->lambdaId, functionId);

                std::vector<Instruction> functionBytecode;
                functionBytecode.push_back({OP_MO_KHOI, 0, 0, 0});
                for (std::size_t index = 0;
                     index < lambda->parameters.size(); ++index) {
                    const IrParameter &parameter = lambda->parameters[index];
                    const int slot = slotFor(parameter.name);
                    functionBytecode.push_back({OP_KHOI_TAO, 0, slot, 0});
                    if (parameter.hasDefault) {
                        const IrValue *defaultValue =
                            program.value(parameter.defaultValue);
                        if (defaultValue == nullptr) {
                            throw std::logic_error(
                                "direct IR lambda parameter has no default value");
                        }
                        const int defaultIndex = StringPool::storeString(
                            encodeDefaultValue(*defaultValue));
                        functionBytecode.push_back(
                            {OP_PARAM_MAC_DINH, defaultIndex, slot,
                             static_cast<int>(index)});
                    } else {
                        functionBytecode.push_back(
                            {OP_PARAM, 0, slot, static_cast<int>(index)});
                    }
                }

                emitInstruction(lambda->body, functionBytecode, false);
                functionBytecode.push_back({OP_DONG_KHOI, 0, 0, 0});
                hamMap::hamBytecodeMap[functionId] =
                    std::move(functionBytecode);
                output.push_back({OP_BIEN_SO, functionId, 0, 0});
                return;
            }
            case IrValueOpcode::LegacyRegion:
                throw std::logic_error("unsupported value reached direct IR emitter");
        }
    }

    void emitInstruction(const IrInstruction &instruction,
                         std::vector<Instruction> &output,
                         bool blockMarkers = true) {
        switch (instruction.opcode) {
            case IrOpcode::NoOp:
                return;
            case IrOpcode::Block:
                if (blockMarkers) output.push_back({OP_MO_KHOI, 0, 0, 0});
                for (const IrInstruction &child : instruction.children) {
                    emitInstruction(child, output);
                }
                if (blockMarkers) output.push_back({OP_DONG_KHOI, 0, 0, 0});
                return;
            case IrOpcode::Conditional: {
                output.push_back({OP_NEU, 0, 0, 0});
                emitValue(instruction.expressionRoots.front(), output);
                const std::size_t falseJump = output.size();
                output.push_back({OP_JUMP_IF_FALSE, 0, 0, 0});
                emitInstruction(instruction.children.front(), output);

                if (instruction.conditionalForm ==
                    vietvm::frontend::AstConditionalForm::IfElseBlocks) {
                    const std::size_t endJump = output.size();
                    output.push_back({OP_JUMP, 0, 0, 0});
                    output[falseJump].operand = static_cast<int>(output.size());
                    emitInstruction(instruction.children[1], output);
                    output[endJump].operand = static_cast<int>(output.size());
                } else {
                    output[falseJump].operand = static_cast<int>(output.size());
                }
                return;
            }
            case IrOpcode::Loop: {
                const IrValue *init = program.value(
                    instruction.expressionRoots[0]);
                const IrValue *target = init == nullptr || init->operands.empty()
                    ? nullptr
                    : program.value(init->operands.front());
                if (target == nullptr) {
                    throw std::logic_error("direct IR loop has no init target");
                }
                const int initSlot = slotFor(target->text);
                // The legacy loop wrapper emits KHỞI_TẠO on every loop,
                // including when the textual slot already exists.
                output.push_back({OP_KHOI_TAO, 0, initSlot, 0});
                emitValue(init->id, output);
                output.push_back({OP_LAP, 0, 0, 0});
                const int conditionTarget = static_cast<int>(output.size());
                output.push_back({OP_DIEU_KIEN, 0, 0, 0});
                emitValue(instruction.expressionRoots[1], output);
                const std::size_t exitJump = output.size();
                output.push_back({OP_JUMP_IF_FALSE, 0, 0, 0});
                emitInstruction(instruction.children.front(), output);
                output.push_back({OP_CAP_NHAT, 0, 0, 0});
                emitValue(instruction.expressionRoots[2], output);
                output.push_back({OP_JUMP, conditionTarget, 0, 0});
                output[exitJump].operand = static_cast<int>(output.size());
                return;
            }
            case IrOpcode::Switch:
                emitValue(instruction.expressionRoots.front(), output);
                output.push_back({OP_CHON, 0, 0, 0});
                for (const IrSwitchArm &arm : instruction.switchArms) {
                    if (arm.kind ==
                        vietvm::frontend::AstSwitchArmKind::Default) {
                        output.push_back({OP_MAC_DINH, 0, 0, 0});
                    } else {
                        const IrValue *label = program.value(arm.label);
                        if (label == nullptr) {
                            throw std::logic_error(
                                "direct IR switch has no case label");
                        }
                        if (label->opcode == IrValueOpcode::ConstInt) {
                            output.push_back(
                                {OP_CA, std::stoi(label->text), -1, 0});
                        } else if (label->opcode ==
                                   IrValueOpcode::ConstString) {
                            const int poolIndex = StringPool::storeString(
                                stripQuotes(label->text));
                            output.push_back({OP_CA, 0, poolIndex, 0});
                        } else if (label->opcode ==
                                   IrValueOpcode::LoadName) {
                            const std::string legacyName =
                                normalizeTokenForCompare(label->text);
                            if (legacyName.empty()) {
                                throw std::logic_error(
                                    "direct IR switch has an empty normalized label");
                            }
                            output.push_back(
                                {OP_CA, slotFor(legacyName), -2, 0});
                        } else {
                            throw std::logic_error(
                                "unsupported direct IR switch label");
                        }
                    }
                    emitInstruction(
                        instruction.children[arm.bodyChildIndex], output);
                }
                return;
            case IrOpcode::Break:
                output.push_back({OP_THOAT, 0, 0, 0});
                return;
            case IrOpcode::Try: {
                const std::size_t tryBegin = output.size();
                output.push_back({OP_THU, 0, -1, 0});
                emitInstruction(instruction.children[0], output);
                const std::size_t tryEnd = output.size();
                output.push_back({OP_THU_KET_THUC, 0, 0, 0});
                output[tryBegin].operand = static_cast<int>(output.size());

                const int catchSlot = instruction.catchVariable.empty()
                    ? -1
                    : slotFor(instruction.catchVariable);
                output.push_back({OP_BAT_LOI, 0, catchSlot, 0});
                emitInstruction(instruction.children[1], output);
                output[tryEnd].operand = static_cast<int>(output.size());
                return;
            }
            case IrOpcode::Throw:
                if (instruction.expressionRoots.empty()) {
                    const int messageIndex = StringPool::storeString(
                        vietvm::messages::messageText(
                            vietvm::messages::kVmUnknownThrownValue));
                    output.push_back({OP_CHUOI, 0, messageIndex, 0});
                } else {
                    emitValue(instruction.expressionRoots.front(), output);
                }
                output.push_back({OP_NEM, 0, 0, 0});
                return;
            case IrOpcode::Continue:
                output.push_back({OP_BO_QUA, 0, 0, 0});
                return;
            case IrOpcode::Return:
                if (instruction.expressionRoots.empty()) {
                    output.push_back({OP_BIEN_SO, 0, 0, 0});
                } else {
                    emitValue(instruction.expressionRoots.front(), output);
                }
                output.push_back({OP_TRA_VE, 0, 0, 0});
                return;
            case IrOpcode::Print:
                emitValue(instruction.expressionRoots.front(), output);
                output.push_back({OP_IN, 0, 0, 0});
                return;
            case IrOpcode::Statement:
                emitValue(instruction.expressionRoots.front(), output);
                return;
            default:
                throw std::logic_error("unsupported statement reached direct IR emitter");
        }
    }

    void emitFunction(const IrInstruction &instruction) {
        const auto function = functionIdsBySymbol.find(instruction.symbolId);
        const auto name = functionNameIndices.find(instruction.symbolId);
        if (function == functionIdsBySymbol.end() || name == functionNameIndices.end()) {
            throw std::logic_error("direct IR function was not predeclared");
        }

        std::vector<Instruction> functionBytecode;
        functionBytecode.push_back({OP_MO_KHOI, 0, 0, 0});
        for (std::size_t index = 0; index < instruction.parameters.size(); ++index) {
            const IrParameter &parameter = instruction.parameters[index];
            const int slot = slotFor(parameter.name);
            functionBytecode.push_back({OP_KHOI_TAO, 0, slot, 0});
            if (parameter.hasDefault) {
                const IrValue *defaultValue = program.value(parameter.defaultValue);
                if (defaultValue == nullptr) {
                    throw std::logic_error("direct IR parameter has no default value");
                }
                const int defaultIndex = StringPool::storeString(
                    encodeDefaultValue(*defaultValue));
                functionBytecode.push_back(
                    {OP_PARAM_MAC_DINH, defaultIndex, slot, static_cast<int>(index)});
            } else {
                functionBytecode.push_back(
                    {OP_PARAM, 0, slot, static_cast<int>(index)});
            }
        }

        emitInstruction(instruction.children.front(), functionBytecode, false);
        functionBytecode.push_back({OP_DONG_KHOI, 0, 0, 0});
        hamMap::hamBytecodeMap[function->second] = std::move(functionBytecode);
        bytecode.push_back({OP_HAM, name->second, function->second, 0});
    }

    void emitClass(const IrInstruction &instruction) {
        const IrInstruction &body = instruction.children.front();
        for (const IrInstruction &member : body.children) {
            if (member.opcode == IrOpcode::NoOp) continue;
            allocateFunction(member);
            emitFunction(member);
        }
    }

    void emitProgram(bool emitMainCall) {
        predeclareFunctions();
        for (const IrInstruction &instruction : program.instructions) {
            if (instruction.opcode == IrOpcode::DefineFunction) {
                emitFunction(instruction);
            } else if (instruction.opcode == IrOpcode::DefineClass) {
                emitClass(instruction);
            } else {
                emitInstruction(instruction, bytecode);
            }
        }
        if (!emitMainCall) return;
        const auto main = slots.find("main");
        if (main != slots.end()) {
            // The compatibility compiler performs this lookup in its shared
            // textual symbol table, not in the function registry. Preserve
            // that observable contract while VM slots and function IDs still
            // share one numeric namespace.
            bytecode.push_back({OP_GOI, 0, main->second, 0});
        }
        bytecode.push_back({OP_DUNG_CHUONG_TRINH, 0, 0, 0});
    }
};

} // namespace

DirectIrSupport analyzeDirectIrSupport(const IrProgram &program) {
    DirectIrSupport support{true, 0};
    SupportContext context;
    const auto collectFunctions = [&](const auto &self,
                                      const IrInstruction &instruction,
                                      bool insideClass) -> void {
        if (instruction.opcode == IrOpcode::DefineFunction &&
            instruction.symbolId >= 0) {
            context.functionSymbols.insert(instruction.symbolId);
            context.functionNames.insert(instruction.declarationName);
            if (insideClass) {
                context.methodSymbols.insert(instruction.symbolId);
                const std::size_t separator =
                    instruction.declarationName.rfind('.');
                context.methodRawNames.insert(
                    separator == std::string::npos
                        ? instruction.declarationName
                        : instruction.declarationName.substr(separator + 1));
                context.methodDeclarationOffsets.emplace(
                    instruction.symbolId, instruction.span.begin.offset);
                context.methodNameDeclarationOffsets.emplace(
                    instruction.declarationName,
                    instruction.span.begin.offset);
            }
        }
        const bool nestedInsideClass = insideClass ||
            instruction.opcode == IrOpcode::DefineClass;
        for (const IrInstruction &child : instruction.children) {
            self(self, child, nestedInsideClass);
        }
    };
    for (const IrInstruction &instruction : program.instructions) {
        collectFunctions(collectFunctions, instruction, false);
    }
    for (const IrInstruction &instruction : program.instructions) {
        if (!supportsInstruction(program, instruction, instruction, context,
                                 false, false, 0, true)) {
            support.supported = false;
            ++support.fallbackRegions;
        }
    }
    return support;
}

std::vector<Instruction> emitDirectBytecode(const IrProgram &program,
                                            bool emitMainCall) {
    const DirectIrSupport support = analyzeDirectIrSupport(program);
    if (!support.supported) {
        throw std::logic_error("program contains IR regions unsupported by direct bytecode emission");
    }

    Emitter emitter{program};
    emitter.emitProgram(emitMainCall);
    return std::move(emitter.bytecode);
}

const char *bytecodeBackendName(BytecodeBackend backend) noexcept {
    switch (backend) {
        case BytecodeBackend::DirectIr: return "direct-ir";
        case BytecodeBackend::LegacyTokenBridge: return "legacy-token-bridge";
    }
    return "legacy-token-bridge";
}

} // namespace vietvm::compiler
