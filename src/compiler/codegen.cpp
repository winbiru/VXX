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
#include "compiler/compileRegistry.h"
#include "frontend/lexer.h"
#include "vpp/bytecode/literal_wire.h"
#include "vpp/core/message_constants.h"

namespace vietvm::compiler {
namespace {

using vietvm::bytecode::escapeLiteralWireField;

// Kiểm tra điều kiện của `isBinaryOperator`.
bool isBinaryOperator(const std::string &op) noexcept {
    return op == "+" || op == "-" || op == "*" || op == "/" || op == "%" ||
           op == "==" || op == "!=" || op == "<" || op == ">" ||
           op == "<=" || op == ">=" || op == "&&" || op == "||";
}

// Kiểm tra điều kiện của `isCompoundAssignment`.
bool isCompoundAssignment(const std::string &op) noexcept {
    return op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=";
}

// Tách runtime thành viên tên; hàm chia dữ liệu đầu vào theo quy tắc phân cách trong khi tôn trọng cấu trúc lồng nhau nếu có.
std::pair<std::string, std::string> splitRuntimeMemberName(
    const std::string &name) {
    const std::size_t separator = name.find('.');
    if (separator == std::string::npos || separator == 0 ||
        separator + 1 >= name.size()) {
        return {};
    }
    return {name.substr(0, separator), name.substr(separator + 1)};
}

// Kiểm tra điều kiện của `isExactSourceToken`.
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

// Kiểm tra điều kiện của `isExactSourceName`.
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

// Kiểm tra điều kiện của `endsWithSourceToken`.
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

// Tìm token nguồn đầu tiên tương ứng với một `IrValue`; hàm so span/lexeme trong token của instruction để kiểm tra parity chính xác.
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

// Kiểm tra điều kiện của `endsWithStatementToken`.
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

// Kiểm tra điều kiện của `hasExactStatementLexemes`.
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

// Kiểm tra điều kiện của `startsWithDedicatedCallSyntax`.
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

// Kiểm tra điều kiện của `containsCall`.
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

// Kiểm tra expression có bao phủ toàn bộ phần biểu thức của statement hay không; hàm so biên token/span để loại trường hợp chỉ parse được tiền tố.
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

// Kiểm tra điều kiện của `hasDelimitedLiteralBounds`.
bool hasDelimitedLiteralBounds(const IrInstruction &instruction,
                               const IrValue &value,
                               const char *opening,
                               const char *closing) noexcept {
    bool startsWithOpening = false;
    bool endsWithClosing = false;
    for (const vietvm::frontend::Token &token : instruction.tokens) {
        if (token.span.begin.offset == value.span.begin.offset) {
            startsWithOpening = token.lexeme == opening;
        }
        if (token.span.end.offset == value.span.end.offset) {
            endsWithClosing = token.lexeme == closing;
        }
    }
    return startsWithOpening && endsWithClosing;
}

// Kiểm tra điều kiện của `isMapKey`.
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

// Kiểm tra điều kiện của `isLegacyScalarLiteral`.
bool isLegacyScalarLiteral(const IrInstruction &instruction,
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

// Kiểm tra điều kiện của `isCollectionLiteralValue`.
bool isCollectionLiteralValue(const IrProgram &program,
                              const IrInstruction &instruction,
                              const IrValue *value) noexcept {
    if (isLegacyScalarLiteral(instruction, value)) return true;
    if (value == nullptr) return false;

    if (value->opcode == IrValueOpcode::ListLiteral) {
        if (!hasDelimitedLiteralBounds(instruction, *value, "[", "]")) return false;
        for (IrValueId element : value->operands) {
            if (!isCollectionLiteralValue(
                    program, instruction, program.value(element))) {
                return false;
            }
        }
        return true;
    }

    if (value->opcode == IrValueOpcode::MapLiteral) {
        if (!hasDelimitedLiteralBounds(instruction, *value, "{", "}") ||
            value->operands.size() % 2 != 0) {
            return false;
        }
        for (std::size_t index = 0; index < value->operands.size(); index += 2) {
            if (!isMapKey(instruction, program.value(value->operands[index])) ||
                !isCollectionLiteralValue(
                    program, instruction, program.value(value->operands[index + 1]))) {
                return false;
            }
        }
        return true;
    }

    return false;
}

// Kiểm tra điều kiện của `isDirectIndexBase`.
bool isDirectIndexBase(const IrValue *value) noexcept {
    return value != nullptr &&
           (value->opcode == IrValueOpcode::LoadName ||
            value->opcode == IrValueOpcode::Index);
}

enum class ValueContext {
    Nested,
    ExpressionStatementRoot,
    OtherStatementRoot,
    DedicatedCallStatementRoot,
    SimpleAssignmentRhs,
};

// Giữ ngữ cảnh khi kiểm tra direct-IR support, gồm tập hàm/phương thức đã thấy và thông tin cần để quyết định một lệnh có thể codegen trực tiếp hay không.
struct SupportContext {
    std::unordered_set<int> functionSymbols;
    std::unordered_set<std::string> functionNames;
    std::unordered_set<int> methodSymbols;
    std::unordered_map<int, std::size_t> methodDeclarationOffsets;
    std::unordered_map<std::string, std::size_t> methodNameDeclarationOffsets;
};

// Kiểm tra phương thức đã được cấp function id trước điểm gọi hiện tại; direct codegen dùng điều kiện này để giữ timing tương thích compiler cũ.
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

// Kiểm tra điều kiện của `supportsInstruction`.
bool supportsInstruction(const IrProgram &program,
                         const IrInstruction &instruction,
                         const IrInstruction &sourceOwner,
                         const SupportContext &context,
                         bool insideFunction,
                         bool insideBlock,
                         std::size_t switchDepth,
                         bool topLevel);

// Kiểm tra điều kiện của `hasExactLambdaSourceShape`.
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

// Kiểm tra điều kiện của `containsLambdaValue`.
bool containsLambdaValue(const IrProgram &program,
                         IrValueId id,
                         std::unordered_set<IrValueId> &visited);

// Kiểm tra điều kiện của `containsLambdaInstruction`.
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

// Kiểm tra điều kiện của `containsLambdaValue`.
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

// Kiểm tra điều kiện của `supportsValue`.
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

        case IrValueOpcode::LoadProperty: {
            const auto member = splitRuntimeMemberName(value->text);
            supported = value->operands.empty() && !member.first.empty() &&
                        isExactSourceName(sourceOwner, *value);
            break;
        }

        case IrValueOpcode::MapLiteral:
            // Composite literals are first-class IR values.  The direct
            // emitter can materialize them anywhere a value is accepted,
            // including call arguments and nested list/map values.
            supported = isCollectionLiteralValue(program, sourceOwner, value);
            break;

        case IrValueOpcode::ListLiteral:
            supported = isCollectionLiteralValue(program, sourceOwner, value);
            break;

        case IrValueOpcode::Index:
            // Indexing is direct-only for now. A chained index remains safe
            // because its innermost base is still a named runtime value.
            supported = value->operands.size() == 2 &&
                        isDirectIndexBase(program.value(value->operands[0])) &&
                        supportsValue(program, sourceOwner, context, value->operands[0],
                                      ValueContext::Nested, visiting) &&
                        supportsValue(program, sourceOwner, context, value->operands[1],
                                      ValueContext::Nested, visiting);
            break;

        case IrValueOpcode::Unary:
            if (value->operands.size() != 1) break;
            if (value->text == "!") {
                supported = supportsValue(program, sourceOwner, context,
                                          value->operands.front(),
                                          ValueContext::Nested, visiting);
                break;
            }
            if (value->text == "-") {
                const IrValue *operand = program.value(value->operands.front());
                supported = operand != nullptr && operand->operands.empty() &&
                            (operand->opcode == IrValueOpcode::ConstInt ||
                             operand->opcode == IrValueOpcode::ConstFloat) &&
                            isExactSourceToken(sourceOwner, *operand);
            }
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

        case IrValueOpcode::StoreProperty: {
            if (valueContext != ValueContext::ExpressionStatementRoot ||
                value->text != "=" || value->operands.size() != 2) {
                break;
            }
            const IrValue *target = program.value(value->operands.front());
            if (target == nullptr || target->opcode != IrValueOpcode::LoadProperty ||
                target->text.empty() || !isExactSourceName(sourceOwner, *target) ||
                value->span.begin.offset != target->span.begin.offset) {
                break;
            }
            supported = supportsValue(program, sourceOwner, context,
                                      value->operands[1],
                                      ValueContext::SimpleAssignmentRhs,
                                      visiting);
            break;
        }

        case IrValueOpcode::StoreIndex: {
            if (valueContext != ValueContext::ExpressionStatementRoot ||
                value->text != "=" || value->operands.size() != 2) {
                break;
            }
            const IrValue *target = program.value(value->operands[0]);
            if (target == nullptr || target->opcode != IrValueOpcode::Index ||
                target->operands.size() != 2) {
                break;
            }
            const IrValue *base = program.value(target->operands[0]);
            supported = isDirectIndexBase(base) &&
                        supportsValue(program, sourceOwner, context, target->operands[0],
                                      ValueContext::Nested, visiting) &&
                        supportsValue(program, sourceOwner, context, target->operands[1],
                                      ValueContext::Nested, visiting) &&
                        supportsValue(program, sourceOwner, context, value->operands[1],
                                      ValueContext::Nested, visiting);
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
                value->callTarget != CallTargetKind::ImportedFunction &&
                value->callTarget != CallTargetKind::ClassConstructor &&
                value->callTarget != CallTargetKind::InstanceMethod &&
                value->callTarget != CallTargetKind::Native &&
                value->callTarget != CallTargetKind::IndirectValue) {
                break;
            }
            if (value->operands.empty() || value->text.empty()) break;
            const IrValue *callee = program.value(value->operands.front());
            if (value->callTarget == CallTargetKind::ClassConstructor) {
                supported = !value->operands.empty() && callee != nullptr &&
                            callee->opcode == IrValueOpcode::LoadName &&
                            callee->text == value->text &&
                            isExactSourceName(sourceOwner, *callee);
            } else if (value->callTarget == CallTargetKind::InstanceMethod) {
                supported = callee != nullptr &&
                            callee->opcode == IrValueOpcode::LoadProperty &&
                            callee->text == value->text &&
                            isExactSourceName(sourceOwner, *callee);
            } else {
                supported = callee != nullptr &&
                            callee->opcode == IrValueOpcode::LoadName &&
                            callee->text == value->text &&
                            isExactSourceName(sourceOwner, *callee) &&
                            methodIsAllocatedBeforeUse(context, *value) &&
                            methodIsAllocatedBeforeUse(context, *callee);
            }
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
                if (!isLegacyScalarLiteral(sourceOwner, defaultValue)) {
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

        case IrValueOpcode::UnsupportedDirectRegion:
            supported = false;
            break;
    }

    visiting.erase(id);
    return supported;
}

// Kiểm tra điều kiện của `supportsFunction`.
bool supportsFunction(const IrProgram &program,
                      const IrInstruction &instruction,
                      const IrInstruction &sourceOwner,
                      const SupportContext &context) {
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
            // Keep this historical name-precedence edge unsupported until
            // symbol-based codegen becomes authoritative.
            return false;
        }
        if (!parameter.hasDefault) {
            if (parameter.defaultValue != kInvalidIrValueId) return false;
            continue;
        }
        const IrValue *defaultValue = program.value(parameter.defaultValue);
        if (!isLegacyScalarLiteral(sourceOwner, defaultValue)) return false;
    }

    return supportsInstruction(program, instruction.children.front(), sourceOwner,
                               context, true, true, 0, false);
}

// Kiểm tra điều kiện của `supportsInstruction`.
bool supportsInstruction(const IrProgram &program,
                         const IrInstruction &instruction,
                         const IrInstruction &sourceOwner,
                         const SupportContext &context,
                         bool insideFunction,
                         bool insideBlock,
                         std::size_t switchDepth,
                         bool topLevel) {
    if (instruction.unsupportedDirectRegion) return false;

    if (instruction.opcode == IrOpcode::NoOp) {
        return instruction.expressionRoots.empty() && instruction.children.empty();
    }
    if (instruction.opcode == IrOpcode::Import) {
        return topLevel &&
               instruction.importForm ==
                   vietvm::frontend::AstImportForm::LocalSourceFile &&
               !instruction.importSpec.target.empty() &&
               instruction.importSpec.hasSemicolon &&
               instruction.expressionRoots.empty() &&
               instruction.children.empty();
    }
    if (instruction.opcode == IrOpcode::DefineFunction) {
        return topLevel && supportsFunction(
            program, instruction, sourceOwner, context);
    }
    if (instruction.opcode == IrOpcode::DefineClass) {
        if (!topLevel ||
            instruction.classForm !=
                vietvm::frontend::AstClassForm::MethodBlock ||
            instruction.declarationName.empty() ||
            !instruction.expressionRoots.empty() ||
            instruction.children.size() != 1 ||
            instruction.children.front().opcode != IrOpcode::Block ||
            instruction.children.front().unsupportedDirectRegion ||
            !instruction.children.front().expressionRoots.empty()) {
            return false;
        }
        for (const IrInstruction &member :
             instruction.children.front().children) {
            if (member.opcode == IrOpcode::NoOp) {
                if (member.unsupportedDirectRegion || !member.expressionRoots.empty() ||
                    !member.children.empty()) {
                    return false;
                }
                continue;
            }
            if (member.opcode != IrOpcode::DefineFunction ||
                !supportsFunction(program, member, sourceOwner, context)) {
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
         !expressionConsumesStatement(sourceOwner, instruction, *root))) {
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

// Ánh xạ toán tử nhị phân IR sang opcode VM tương ứng; hàm trả opcode chính xác cho số học, so sánh và luận lý.
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
    throw std::logic_error(std::string(messages::kInternalDirectIrUnsupportedBinaryOperator));
}

// Ánh xạ toán tử gán kết hợp như `+=`/`-=` sang opcode VM thực hiện phép toán nền tương ứng.
Opcode compoundOpcode(const std::string &op) {
    if (op == "+=") return OP_CONG;
    if (op == "-=") return OP_TRU;
    if (op == "*=") return OP_NHAN;
    if (op == "/=") return OP_CHIA;
    if (op == "%=") return OP_MODULO;
    throw std::logic_error(std::string(messages::kInternalDirectIrUnsupportedCompoundAssignment));
}

// Bỏ dấu nháy của literal chuỗi IR và giải phần bao ngoài trước khi lưu nội dung thực vào `StringPool`.
std::string unquote(const std::string &text) {
    if (text.size() >= 2 &&
        ((text.front() == '"' && text.back() == '"') ||
         (text.front() == '\'' && text.back() == '\''))) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

// Nối thêm tagged scalar giá trị trực tiếp; hàm đưa dữ liệu mới vào cuối cấu trúc đích theo đúng thứ tự hiện có.
bool appendTaggedScalarLiteral(std::ostringstream &encoded,
                               const IrValue &value,
                               char fieldSeparator) {
    switch (value.opcode) {
        case IrValueOpcode::ConstInt:
        case IrValueOpcode::ConstBool:
            encoded << 'i' << fieldSeparator;
            if (value.opcode == IrValueOpcode::ConstBool) {
                encoded << (value.text == "đúng" ? '1' : '0');
            } else {
                encoded << escapeLiteralWireField(value.text);
            }
            return true;
        case IrValueOpcode::ConstFloat:
            encoded << 'd' << fieldSeparator << escapeLiteralWireField(value.text);
            return true;
        case IrValueOpcode::ConstString:
            // Map/list literal payloads decode source escapes before applying
            // the wire escaping. Ordinary string-expression emission keeps
            // its different legacy contract in `unquote` above.
            encoded << 's' << fieldSeparator
                    << escapeLiteralWireField(stripQuotes(value.text));
            return true;
        case IrValueOpcode::ConstNull:
            encoded << 'n' << fieldSeparator;
            return true;
        default:
            return false;
    }
}

// Mã hóa danh sách giá trị trực tiếp; hàm chuyển cấu trúc dữ liệu sang dạng chuỗi/wire ổn định để lưu trong bytecode hoặc truyền qua ranh giới module.
std::string encodeListLiteral(const IrProgram &program, const IrValue &list);

// Mã hóa ánh xạ giá trị trực tiếp; hàm chuyển cấu trúc dữ liệu sang dạng chuỗi/wire ổn định để lưu trong bytecode hoặc truyền qua ranh giới module.
std::string encodeMapLiteral(const IrProgram &program, const IrValue &map) {
    constexpr char recordSeparator = vietvm::bytecode::kLiteralRecordSeparator;
    constexpr char fieldSeparator = vietvm::bytecode::kLiteralFieldSeparator;
    std::ostringstream encoded;
    for (std::size_t index = 0; index < map.operands.size(); index += 2) {
        const IrValue &key = *program.value(map.operands[index]);
        const IrValue &value = *program.value(map.operands[index + 1]);
        if (index != 0) encoded << recordSeparator;
        encoded << escapeLiteralWireField(key.opcode == IrValueOpcode::ConstString
                                              ? stripQuotes(key.text)
                                              : key.text)
                << fieldSeparator;
        if (appendTaggedScalarLiteral(encoded, value, fieldSeparator)) continue;
        if (value.opcode == IrValueOpcode::ListLiteral) {
            encoded << 'l' << fieldSeparator
                    << escapeLiteralWireField(encodeListLiteral(program, value));
            continue;
        }
        if (value.opcode == IrValueOpcode::MapLiteral) {
            encoded << 'm' << fieldSeparator
                    << escapeLiteralWireField(encodeMapLiteral(program, value));
            continue;
        }
        throw std::logic_error(std::string(messages::kInternalDirectIrUnsupportedMapValue));
    }
    return encoded.str();
}

// Mã hóa danh sách giá trị trực tiếp; hàm chuyển cấu trúc dữ liệu sang dạng chuỗi/wire ổn định để lưu trong bytecode hoặc truyền qua ranh giới module.
std::string encodeListLiteral(const IrProgram &program, const IrValue &list) {
    constexpr char recordSeparator = vietvm::bytecode::kLiteralRecordSeparator;
    constexpr char fieldSeparator = vietvm::bytecode::kLiteralFieldSeparator;
    std::ostringstream encoded;
    for (std::size_t index = 0; index < list.operands.size(); ++index) {
        const IrValue &value = *program.value(list.operands[index]);
        if (index != 0) encoded << recordSeparator;
        if (appendTaggedScalarLiteral(encoded, value, fieldSeparator)) continue;
        if (value.opcode == IrValueOpcode::ListLiteral) {
            encoded << 'l' << fieldSeparator
                    << escapeLiteralWireField(encodeListLiteral(program, value));
            continue;
        }
        if (value.opcode == IrValueOpcode::MapLiteral) {
            encoded << 'm' << fieldSeparator
                    << escapeLiteralWireField(encodeMapLiteral(program, value));
            continue;
        }
        throw std::logic_error(std::string(messages::kInternalDirectIrUnsupportedListValue));
    }
    return encoded.str();
}

// Mã hóa mặc định giá trị; hàm chuyển cấu trúc dữ liệu sang dạng chuỗi/wire ổn định để lưu trong bytecode hoặc truyền qua ranh giới module.
std::string encodeDefaultValue(const IrValue &value) {
    switch (value.opcode) {
        case IrValueOpcode::ConstInt: return "i:" + value.text;
        case IrValueOpcode::ConstFloat: return "d:" + value.text;
        case IrValueOpcode::ConstString: return "s:" + stripQuotes(value.text);
        case IrValueOpcode::ConstBool:
            return value.text == "đúng" ? "i:1" : "i:0";
        case IrValueOpcode::ConstNull: return "n:";
        default:
            throw std::logic_error(std::string(messages::kInternalDirectIrUnsupportedDefaultValue));
    }
}

// Giữ trạng thái của direct-IR codegen trong một lượt phát mã; struct quản lý bytecode, slot, function id, lambda và thứ tự phát lớp cha/con.
struct Emitter {
    CompilationRegistryState &registry;
    const IrProgram &program;
    const std::unordered_map<std::string, Opcode> &keywordMap;
    std::vector<Instruction> bytecode;
    std::unordered_map<std::string, int> slots;
    std::unordered_map<int, int> functionIdsBySymbol;
    std::unordered_map<std::string, int> functionIdsByName;
    std::unordered_map<int, int> functionNameIndices;
    std::unordered_map<IrLambdaId, int> lambdaIds;
    std::unordered_map<std::string, const IrInstruction *> classesByName;
    std::unordered_set<std::string> emittedClasses;
    std::unordered_set<std::string> emittingClasses;
    int nextSlot = 0;

    // Quản lý class context theo RAII; guard đẩy tên lớp trước khi phát method và tự pop khi rời scope để lookup visibility/`gốc` không rò sang lớp kế tiếp.
    struct ClassContextGuard {
        CompilationRegistryState &registry;

        // Đưa tên lớp hiện tại lên class-context stack trước khi codegen các phương thức của lớp đó.
        ClassContextGuard(CompilationRegistryState &state,
                          const std::string &className)
            : registry(state) {
            pushClassContext(registry, className);
        }
        // Gỡ class context vừa đẩy khi codegen lớp kết thúc, kể cả khi việc phát bytecode ném exception.
        ~ClassContextGuard() { popClassContext(registry); }

        // Cấm sao chép vì mỗi guard phải pop đúng một context mà chính nó đã push.
        ClassContextGuard(const ClassContextGuard &) = delete;
        // Cấm phép gán để không làm thay đổi ownership của thao tác pop class context.
        ClassContextGuard &operator=(const ClassContextGuard &) = delete;
    };

    // Khởi tạo emitter cho một `IrProgram`; object giữ tham chiếu IR/keyword map và bắt đầu với các bảng slot/function/class rỗng cho lượt phát mã hiện tại.
    Emitter(CompilationRegistryState &state,
            const IrProgram &ir,
            const std::unordered_map<std::string, Opcode> &keywords)
        : registry(state), program(ir), keywordMap(keywords) {}

    // Cấp phát hàm; hàm lấy mã định danh hoặc vùng lưu trữ mới và đăng ký nó vào trạng thái quản lý hiện tại.
    void allocateFunction(const IrInstruction &instruction) {
        if (functionIdsBySymbol.find(instruction.symbolId) !=
            functionIdsBySymbol.end()) {
            return;
        }
        const int functionId = registry.allocateFunctionId();
        const int nameIndex = registry.storeString(instruction.declarationName);
        functionIdsBySymbol.emplace(instruction.symbolId, functionId);
        functionIdsByName.emplace(instruction.declarationName, functionId);
        functionNameIndices.emplace(instruction.symbolId, nameIndex);
        slots.emplace(instruction.declarationName, functionId);
        registry.functionBytecode[functionId] = {};
        registry.setFunctionNameIndex(functionId, nameIndex);
        if (functionId >= nextSlot) nextSlot = functionId + 1;
    }

    // Quét các lệnh top-level trước khi phát thân hàm; hàm cấp function id sớm và lập bảng lớp để hỗ trợ đệ quy/forward reference khi codegen.
    void predeclareFunctions() {
        for (const IrInstruction &instruction : program.instructions) {
            if (instruction.opcode == IrOpcode::DefineFunction) {
                allocateFunction(instruction);
            } else if (instruction.opcode == IrOpcode::DefineClass) {
                classesByName.emplace(instruction.declarationName, &instruction);
            }
        }
    }

    // Trả slot VM gắn với một tên; hàm tái sử dụng slot đã cấp hoặc tạo slot mới bằng `nextSlot` rồi ghi vào bảng `slots`.
    int slotFor(const std::string &name) {
        const auto found = slots.find(name);
        if (found != slots.end()) return found->second;
        const int slot = nextSlot++;
        slots.emplace(name, slot);
        return slot;
    }

    // Phát mã cho lời gọi đối số; hàm duyệt biểu diễn đầu vào và sinh opcode/metadata tương ứng vào buffer bytecode đích.
    void emitCallArguments(const IrValue &call,
                           std::vector<Instruction> &output) {
        for (std::size_t index = 1; index < call.operands.size(); ++index) {
            emitValue(call.operands[index], output);
        }
    }

    // Phát mã cho tham số bindings; hàm duyệt biểu diễn đầu vào và sinh opcode/metadata tương ứng vào buffer bytecode đích.
    void emitParameterBindings(const std::vector<IrParameter> &parameters,
                               std::vector<Instruction> &output,
                               std::string_view missingDefaultMessage) {
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            const IrParameter &parameter = parameters[index];
            const int slot = slotFor(parameter.name);
            output.push_back({OP_KHOI_TAO, 0, slot, 0});
            if (!parameter.hasDefault) {
                output.push_back({OP_PARAM, 0, slot, static_cast<int>(index)});
                continue;
            }

            const IrValue *defaultValue = program.value(parameter.defaultValue);
            if (defaultValue == nullptr) {
                throw std::logic_error(std::string(missingDefaultMessage));
            }
            const int defaultIndex = registry.storeString(
                encodeDefaultValue(*defaultValue));
            output.push_back(
                {OP_PARAM_MAC_DINH, defaultIndex, slot, static_cast<int>(index)});
        }
    }

    // Phát một `IrValue` thành chuỗi opcode VM phù hợp; hàm xử lý literal, biến, thuộc tính, index, phép toán, lời gọi và lambda theo từng opcode IR.
    void emitValue(IrValueId id, std::vector<Instruction> &output) {
        const IrValue *value = program.value(id);
        if (value == nullptr) {
            throw std::logic_error(std::string(messages::kInternalDirectIrInvalidValueId));
        }

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
                const int index = registry.storeString(value->text);
                output.push_back({OP_BIEN_SO_FLOAT, 0, index, 0});
                return;
            }
            case IrValueOpcode::ConstString: {
                const int index = registry.storeString(unquote(value->text));
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
                const int index = registry.storeString(encodeMapLiteral(program, *value));
                output.push_back({OP_MAP_LITERAL, 0, index, 0});
                return;
            }
            case IrValueOpcode::ListLiteral: {
                const int index = registry.storeString(encodeListLiteral(program, *value));
                output.push_back({OP_LIST_LITERAL, 0, index, 0});
                return;
            }
            case IrValueOpcode::Index:
                emitValue(value->operands[0], output);
                emitValue(value->operands[1], output);
                output.push_back({OP_DOC_CHI_SO, 0, 0, 0});
                return;
            case IrValueOpcode::LoadName: {
                const auto function = functionIdsBySymbol.find(value->symbolId);
                if (function != functionIdsBySymbol.end()) {
                    output.push_back({OP_BIEN_SO, function->second, 0, 0});
                    return;
                }
                const int registeredFunction =
                    resolveFunctionIdByName(registry, value->text, slots);
                if (registeredFunction >= 0) {
                    output.push_back({OP_BIEN_SO, registeredFunction, 0, 0});
                    return;
                }
                const int slot = slotFor(value->text);
                output.push_back({OP_TEN_BIEN_GIA_TRI, 0, slot, 0});
                return;
            }
            case IrValueOpcode::LoadProperty: {
                const auto member = splitRuntimeMemberName(value->text);
                if (member.first.empty()) {
                    throw std::logic_error(std::string(
                        messages::kInternalDirectIrUnsupportedValue));
                }
                const int receiverSlot = slotFor(member.first);
                const int memberIndex = registry.storeString(member.second);
                output.push_back({OP_TEN_BIEN_GIA_TRI, 0, receiverSlot, 0});
                output.push_back({OP_DOC_THUOC_TINH, 0, memberIndex, 0});
                return;
            }
            case IrValueOpcode::Unary: {
                if (value->text == "-") {
                    const IrValue *operand = program.value(value->operands.front());
                    if (operand == nullptr) {
                        throw std::logic_error(std::string(
                            messages::kInternalDirectIrInvalidValueId));
                    }
                    if (operand->opcode == IrValueOpcode::ConstInt) {
                        try {
                            output.push_back(
                                {OP_BIEN_SO, -std::stoi(operand->text), 0, 0});
                        } catch (...) {
                            throw std::runtime_error(vietvm::messages::formatMessage(
                                vietvm::messages::kInternalNumberParseMismatch,
                                {"-" + operand->text}));
                        }
                        return;
                    }
                    if (operand->opcode == IrValueOpcode::ConstFloat) {
                        const int index = registry.storeString("-" + operand->text);
                        output.push_back({OP_BIEN_SO_FLOAT, 0, index, 0});
                        return;
                    }
                    throw std::logic_error(std::string(
                        messages::kInternalDirectIrUnsupportedValue));
                }
                emitValue(value->operands.front(), output);
                output.push_back({OP_PHU_DINH, 0, 0, 0});
                return;
            }
            case IrValueOpcode::Binary:
                emitValue(value->operands[0], output);
                emitValue(value->operands[1], output);
                output.push_back({binaryOpcode(value->text), 0, 0, 0});
                return;
            case IrValueOpcode::StoreName: {
                const IrValue *target = program.value(value->operands.front());
                if (target == nullptr) {
                    throw std::logic_error(std::string(messages::kInternalDirectIrInvalidStoreTarget));
                }

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
            case IrValueOpcode::StoreProperty: {
                const IrValue *target = program.value(value->operands.front());
                if (target == nullptr || target->opcode != IrValueOpcode::LoadProperty ||
                    value->text != "=" || value->operands.size() != 2) {
                    throw std::logic_error(std::string(
                        messages::kInternalDirectIrInvalidStoreTarget));
                }
                const auto member = splitRuntimeMemberName(target->text);
                if (member.first.empty()) {
                    throw std::logic_error(std::string(
                        messages::kInternalDirectIrInvalidStoreTarget));
                }
                const int receiverSlot = slotFor(member.first);
                const int memberIndex = registry.storeString(member.second);
                output.push_back({OP_TEN_BIEN_GIA_TRI, 0, receiverSlot, 0});
                emitValue(value->operands[1], output);
                output.push_back({OP_GAN_THUOC_TINH, 0, memberIndex, 0});
                return;
            }
            case IrValueOpcode::StoreIndex: {
                const IrValue *target = program.value(value->operands.front());
                if (target == nullptr || target->opcode != IrValueOpcode::Index ||
                    target->operands.size() != 2) {
                    throw std::logic_error(std::string(messages::kInternalDirectIrInvalidIndexedStoreTarget));
                }
                emitValue(target->operands[0], output);
                emitValue(target->operands[1], output);
                emitValue(value->operands[1], output);
                output.push_back({OP_GAN_CHI_SO, 0, 0, 0});
                return;
            }
            case IrValueOpcode::Call: {
                const auto function = functionIdsBySymbol.find(value->symbolId);
                if (function == functionIdsBySymbol.end()) {
                    throw std::logic_error(std::string(messages::kInternalDirectIrMissingVmFunctionMapping));
                }
                std::string resolvedName = value->text;
                int target = -1;
                if (value->explicitCall) {
                    // `gọi` resolves before arguments and only accepts a local
                    // function whose bytecode is already available.
                    resolvedName = resolveCallableNameInContext(
                        registry, value->text, slots);
                    validateCallableAccess(registry, resolvedName);
                    target = resolveFunctionIdByName(
                        registry, resolvedName, slots, false);
                }

                emitCallArguments(*value, output);

                if (!value->explicitCall) {
                    // Ordinary expression calls resolve after arguments. This
                    // preserves the legacy class-context/StringPool timing,
                    // even when semantic analysis already knows the function.
                    resolvedName = resolveCallableNameInContext(
                        registry, value->text, slots);
                    validateCallableAccess(registry, resolvedName);
                    target = resolveFunctionIdByName(registry, value->text, slots);
                }
                if (target < 0) {
                    const int nameIndex = registry.storeString(resolvedName);
                    target = -(nameIndex + 1);
                }
                output.push_back({OP_GOI,
                                  static_cast<int>(value->operands.size() - 1),
                                  target,
                                  0});
                return;
            }
            case IrValueOpcode::CallDynamic: {
                if (value->callTarget == CallTargetKind::ClassConstructor) {
                    if (value->operands.empty()) {
                        throw std::logic_error(std::string(
                            messages::kInternalDirectIrUnsupportedValue));
                    }
                    emitCallArguments(*value, output);
                    const int classNameIndex = registry.storeString(value->text);
                    output.push_back({OP_TAO_DOI_TUONG,
                                      static_cast<int>(value->operands.size() - 1),
                                      classNameIndex,
                                      0});
                    return;
                }

                if (value->callTarget == CallTargetKind::InstanceMethod) {
                    const auto member = splitRuntimeMemberName(value->text);
                    if (member.first.empty()) {
                        throw std::logic_error(std::string(
                            messages::kInternalDirectIrUnsupportedValue));
                    }
                    const int receiverSlot = slotFor(member.first);
                    const int methodNameIndex = registry.storeString(member.second);
                    output.push_back({OP_TEN_BIEN_GIA_TRI, 0, receiverSlot, 0});
                    emitCallArguments(*value, output);
                    output.push_back({OP_GOI_PHUONG_THUC,
                                      static_cast<int>(value->operands.size() - 1),
                                      methodNameIndex,
                                      member.first == "gốc" ? 1 : 0});
                    return;
                }

                std::string resolvedName = value->text;
                if (value->explicitCall) {
                    // `gọi name(...)` resolves the callable before compiling
                    // arguments in the compatibility backend.
                    resolvedName = resolveCallableNameInContext(
                        registry, value->text, slots);
                    validateCallableAccess(registry, resolvedName);
                }
                emitCallArguments(*value, output);
                const int argumentCount =
                    static_cast<int>(value->operands.size() - 1);

                if (!value->explicitCall) {
                    // Ordinary expression calls resolve after argument
                    // emission. StringPool changes from arguments can affect
                    // legacy class qualification, so preserve that ordering.
                    resolvedName = resolveCallableNameInContext(
                        registry, value->text, slots);
                    validateCallableAccess(registry, resolvedName);
                    const int registeredFunction =
                        resolveFunctionIdByName(registry, value->text, slots);
                    if (registeredFunction >= 0) {
                        output.push_back(
                            {OP_GOI, argumentCount, registeredFunction, 0});
                        return;
                    }
                    const auto function = functionIdsByName.find(resolvedName);
                    if (function != functionIdsByName.end()) {
                        output.push_back(
                            {OP_GOI, argumentCount, function->second, 0});
                        return;
                    }
                    const auto variable = slots.find(resolvedName);
                    if (variable != slots.end()) {
                        output.push_back(
                            {OP_TEN_BIEN_GIA_TRI, 0, variable->second, 0});
                        output.push_back(
                            {OP_GOI_GIAN_TIEP, argumentCount, 0, 0});
                        return;
                    }
                }

                const int nameIndex = registry.storeString(resolvedName);
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
                const int functionId = registry.allocateFunctionId();
                lambdaIds.emplace(value->lambdaId, functionId);

                std::vector<Instruction> functionBytecode;
                functionBytecode.push_back({OP_MO_KHOI, 0, 0, 0});
                emitParameterBindings(
                    lambda->parameters, functionBytecode,
                    messages::kInternalDirectIrMissingLambdaDefaultValue);

                emitBlock(lambda->body, functionBytecode, false);
                functionBytecode.push_back({OP_DONG_KHOI, 0, 0, 0});
                registry.functionBytecode[functionId] =
                    std::move(functionBytecode);
                output.push_back({OP_BIEN_SO, functionId, 0, 0});
                return;
            }
            case IrValueOpcode::UnsupportedDirectRegion:
                throw std::logic_error(std::string(messages::kInternalDirectIrUnsupportedValue));
        }
    }

    // Phát mã cho khối; hàm duyệt biểu diễn đầu vào và sinh opcode/metadata tương ứng vào buffer bytecode đích.
    void emitBlock(const IrInstruction &block,
                   std::vector<Instruction> &output,
                   bool blockMarkers = true) {
        if (blockMarkers) output.push_back({OP_MO_KHOI, 0, 0, 0});
        for (const IrInstruction &child : block.children) {
            emitInstruction(child, output);
        }
        if (blockMarkers) output.push_back({OP_DONG_KHOI, 0, 0, 0});
    }

    // Phát một `IrInstruction` sang bytecode hoặc chuyển tiếp đến bộ phát chuyên biệt cho khối, điều kiện, vòng lặp và câu lệnh phức hợp.
    void emitInstruction(const IrInstruction &instruction,
                         std::vector<Instruction> &output) {
        if (instruction.opcode == IrOpcode::Block) {
            emitBlock(instruction, output);
            return;
        }
        emitStatement(instruction, output);
    }

    // Phát mã cho câu lệnh; hàm duyệt biểu diễn đầu vào và sinh opcode/metadata tương ứng vào buffer bytecode đích.
    void emitStatement(const IrInstruction &instruction,
                       std::vector<Instruction> &output) {
        switch (instruction.opcode) {
            case IrOpcode::NoOp:
                return;
            case IrOpcode::Import:
                compileImportSpec(registry, instruction.importSpec, nextSlot, keywordMap);
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
                    throw std::logic_error(std::string(
                        messages::kInternalDirectIrLoopMissingInitializerTarget));
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
                            throw std::logic_error(std::string(
                                messages::kInternalDirectIrSwitchMissingCaseLabel));
                        }
                        if (label->opcode == IrValueOpcode::ConstInt) {
                            output.push_back(
                                {OP_CA, std::stoi(label->text), -1, 0});
                        } else if (label->opcode ==
                                   IrValueOpcode::ConstString) {
                            const int poolIndex = registry.storeString(
                                stripQuotes(label->text));
                            output.push_back({OP_CA, 0, poolIndex, 0});
                        } else if (label->opcode ==
                                   IrValueOpcode::LoadName) {
                            const std::string legacyName =
                                normalizeTokenForCompare(label->text);
                            if (legacyName.empty()) {
                                throw std::logic_error(std::string(
                                    messages::kInternalDirectIrSwitchEmptyNormalizedLabel));
                            }
                            output.push_back(
                                {OP_CA, slotFor(legacyName), -2, 0});
                        } else {
                            throw std::logic_error(std::string(
                                messages::kInternalDirectIrUnsupportedSwitchLabel));
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
                    const int messageIndex = registry.storeString(
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
                throw std::logic_error(std::string(
                    messages::kInternalDirectIrUnsupportedStatement));
        }
    }

    // Phát mã cho hàm body; hàm duyệt biểu diễn đầu vào và sinh opcode/metadata tương ứng vào buffer bytecode đích.
    std::vector<Instruction> emitFunctionBody(
        const IrInstruction &instruction) {
        std::vector<Instruction> functionBytecode;
        functionBytecode.push_back({OP_MO_KHOI, 0, 0, 0});
        for (const std::string &receiverName : instruction.implicitReceiverNames) {
            const int receiverSlot = slotFor(receiverName);
            functionBytecode.push_back({OP_KHOI_TAO, 0, receiverSlot, 0});
            functionBytecode.push_back({OP_PARAM, 0, receiverSlot, -1});
        }
        emitParameterBindings(
            instruction.parameters, functionBytecode,
            messages::kInternalDirectIrMissingParameterDefaultValue);
        emitBlock(instruction.children.front(), functionBytecode, false);
        functionBytecode.push_back({OP_DONG_KHOI, 0, 0, 0});
        return functionBytecode;
    }

    // Đăng ký metadata của hàm rồi phát thân hàm vào `hamMap`, bảo đảm function id và tên trong `StringPool` nhất quán.
    void emitFunction(const IrInstruction &instruction) {
        const auto function = functionIdsBySymbol.find(instruction.symbolId);
        const auto name = functionNameIndices.find(instruction.symbolId);
        if (function == functionIdsBySymbol.end() || name == functionNameIndices.end()) {
            throw std::logic_error(std::string(
                messages::kInternalDirectIrFunctionNotPredeclared));
        }

        registry.functionBytecode[function->second] = emitFunctionBody(instruction);
        bytecode.push_back({OP_HAM, name->second, function->second, 0});
    }

    // Đăng ký lớp runtime và các phương thức của lớp; lớp cha được mã hóa qua chỉ số `StringPool` để VM nối quan hệ kế thừa.
    void emitClass(const IrInstruction &instruction) {
        ClassContextGuard classContext(registry, instruction.declarationName);
        const int classNameIndex = registry.storeString(instruction.declarationName);
        int encodedSuperclass = 0;
        if (!instruction.superclassName.empty()) {
            encodedSuperclass = registry.storeString(instruction.superclassName) + 1;
        }
        bytecode.push_back({OP_TAO_LOP, 0, classNameIndex, encodedSuperclass});
        const IrInstruction &body = instruction.children.front();
        for (const IrInstruction &member : body.children) {
            if (member.opcode == IrOpcode::NoOp) continue;
            allocateFunction(member);
            emitFunction(member);
            const auto function = functionIdsBySymbol.find(member.symbolId);
            if (function == functionIdsBySymbol.end()) {
                throw std::logic_error(std::string(
                    messages::kInternalDirectIrFunctionNotPredeclared));
            }
            std::string methodName = member.declarationName;
            const std::string prefix = instruction.declarationName + ".";
            if (methodName.rfind(prefix, 0) == 0) {
                methodName.erase(0, prefix.size());
            }
            const int methodNameIndex = registry.storeString(methodName);
            int encodedClassNameIndex = classNameIndex;
            int encodedFunctionId = function->second;
            if (member.effectiveVisibility == SemanticVisibility::Private) {
                encodedFunctionId = -(encodedFunctionId + 1);
            } else if (member.effectiveVisibility == SemanticVisibility::Protected) {
                encodedClassNameIndex = -(encodedClassNameIndex + 1);
            }
            bytecode.push_back({OP_THEM_PHUONG_THUC,
                                encodedClassNameIndex,
                                methodNameIndex,
                                encodedFunctionId});
        }
    }

    // Phát lớp theo thứ tự cha trước con; hàm đệ quy qua metadata superclass và chặn chu trình bằng tập lớp đang phát.
    void emitClassWithSuperclass(const IrInstruction &instruction) {
        if (emittedClasses.find(instruction.declarationName) != emittedClasses.end()) return;
        if (!emittingClasses.insert(instruction.declarationName).second) {
            throw std::logic_error(vietvm::messages::messageText(
                messages::kInternalDirectIrInheritanceCycle,
                {instruction.declarationName}));
        }
        if (!instruction.superclassName.empty()) {
            const auto parent = classesByName.find(instruction.superclassName);
            if (parent == classesByName.end()) {
                throw std::logic_error(vietvm::messages::messageText(
                    messages::kInternalDirectIrSuperclassNotFound,
                    {instruction.superclassName}));
            }
            emitClassWithSuperclass(*parent->second);
        }
        emitClass(instruction);
        emittingClasses.erase(instruction.declarationName);
        emittedClasses.insert(instruction.declarationName);
    }

    // Phát toàn bộ chương trình IR theo thứ tự top-level rồi thêm lời gọi `main`/opcode kết thúc theo hợp đồng bytecode hiện tại.
    void emitProgram(bool emitMainCall) {
        predeclareFunctions();
        for (const IrInstruction &instruction : program.instructions) {
            if (instruction.opcode == IrOpcode::DefineFunction) {
                emitFunction(instruction);
            } else if (instruction.opcode == IrOpcode::DefineClass) {
                emitClassWithSuperclass(instruction);
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

// Duyệt IR để xác định phần nào backend direct IR có thể phát bytecode mà không cần rơi về đường biên dịch cũ.
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
            ++support.unsupportedRegions;
        }
    }
    return support;
}

// Phát bytecode trực tiếp từ IR đã được xác nhận hỗ trợ; emitter ánh xạ lệnh/giá trị IR thành opcode VM và metadata tương ứng.
std::vector<Instruction> emitDirectBytecode(CompilationRegistryState &state,
                                            const IrProgram &program,
                                            const std::unordered_map<std::string, Opcode> &keywordMap,
                                            bool emitMainCall) {
    const DirectIrSupport support = analyzeDirectIrSupport(program);
    if (!support.supported) {
        throw std::logic_error(std::string(
            messages::kInternalDirectIrProgramHasUnsupportedRegion));
    }

    Emitter emitter{state, program, keywordMap};
    emitter.emitProgram(emitMainCall);
    return std::move(emitter.bytecode);
}

} // namespace vietvm::compiler
