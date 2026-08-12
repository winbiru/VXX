#include <iostream>
#include <string>
#include <vector>

#include "vpp/compiler/ir.h"
#include "vpp/compiler/optimizer.h"

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

vietvm::frontend::Token token(const std::string &lexeme) {
    return {vietvm::frontend::TokenKind::Identifier, lexeme, {}};
}

vietvm::frontend::ExprId appendExpression(
    vietvm::frontend::AstProgram &program,
    vietvm::frontend::AstExpression expression) {
    expression.id = program.expressions.size();
    program.expressions.push_back(std::move(expression));
    return program.expressions.back().id;
}

vietvm::frontend::AstExpression nameExpression(const std::string &name) {
    vietvm::frontend::AstExpression expression;
    expression.kind = vietvm::frontend::AstExpressionKind::Name;
    expression.text = name;
    return expression;
}

vietvm::frontend::AstExpression integerExpression(const std::string &value) {
    vietvm::frontend::AstExpression expression;
    expression.kind = vietvm::frontend::AstExpressionKind::Literal;
    expression.literalKind = vietvm::frontend::AstLiteralKind::Integer;
    expression.text = value;
    return expression;
}

void testRecursiveExpressionLowering() {
    using namespace vietvm::frontend;
    using namespace vietvm::compiler;

    AstProgram program;
    for (const char *lexeme : {"f", "(", "x", "=", "1", "+", "2", ")", ";"}) {
        program.tokens.push_back(token(lexeme));
    }

    const ExprId callee = appendExpression(program, nameExpression("f"));
    const ExprId target = appendExpression(program, nameExpression("x"));
    const ExprId one = appendExpression(program, integerExpression("1"));
    const ExprId two = appendExpression(program, integerExpression("2"));

    AstExpression addition;
    addition.kind = AstExpressionKind::Binary;
    addition.text = "+";
    addition.left = one;
    addition.right = two;
    const ExprId sum = appendExpression(program, std::move(addition));

    AstExpression assignment;
    assignment.kind = AstExpressionKind::Assignment;
    assignment.text = "=";
    assignment.left = target;
    assignment.right = sum;
    const ExprId store = appendExpression(program, std::move(assignment));

    AstExpression call;
    call.kind = AstExpressionKind::Call;
    call.callee = callee;
    call.arguments.push_back(store);
    const ExprId callRoot = appendExpression(program, std::move(call));

    AstStatement statement;
    statement.kind = AstStatementKind::Expression;
    statement.tokenEnd = program.tokens.size();
    statement.expressionRoots.push_back(callRoot);
    program.statements.push_back(std::move(statement));

    const IrProgram ir = lowerToIr(program, {});
    expect(ir.instructions.size() == 1 &&
               ir.instructions.front().expressionRoots.size() == 1,
           "expression statement has one structured IR root");
    if (ir.instructions.empty() || ir.instructions.front().expressionRoots.empty()) return;

    const IrValue *callValue = ir.value(ir.instructions.front().expressionRoots.front());
    expect(callValue != nullptr && callValue->opcode == IrValueOpcode::CallDynamic,
           "a call stays explicitly dynamic until semantic binding is keyed by AstExprId");
    expect(callValue != nullptr && callValue->sourceExprId == callRoot,
           "IR call retains its stable source expression identity");
    expect(callValue != nullptr && callValue->operands.size() == 2,
           "call IR stores the callee first and then source-order arguments");
    if (callValue == nullptr || callValue->operands.size() != 2) return;

    const IrValue *calleeValue = ir.value(callValue->operands[0]);
    const IrValue *storeValue = ir.value(callValue->operands[1]);
    expect(calleeValue != nullptr && calleeValue->opcode == IrValueOpcode::LoadName &&
               calleeValue->text == "f",
           "call callee recursively lowers to a name load");
    expect(storeValue != nullptr && storeValue->opcode == IrValueOpcode::StoreName &&
               storeValue->text == "=" && storeValue->operands.size() == 2,
           "assignment recursively lowers to an ordered name store");
    if (storeValue == nullptr || storeValue->operands.size() != 2) return;

    const IrValue *binaryValue = ir.value(storeValue->operands[1]);
    expect(binaryValue != nullptr && binaryValue->opcode == IrValueOpcode::Binary &&
               binaryValue->text == "+" && binaryValue->operands.size() == 2,
           "assignment value retains the recursive binary-expression topology");
    expect(ir.legacyRegionCount == 0,
           "fully represented expression trees do not claim a legacy region");

    SemanticModel boundSemantic;
    BindingResult calleeBinding;
    calleeBinding.expression = callee;
    calleeBinding.kind = BindingKind::Symbol;
    calleeBinding.symbol = 7;
    boundSemantic.expressionBindings.push_back(std::move(calleeBinding));
    CallBinding directCall;
    directCall.expression = callRoot;
    directCall.callee = callee;
    directCall.kind = CallTargetKind::DirectFunction;
    directCall.symbol = 7;
    directCall.runtimeName = "f";
    boundSemantic.callBindings.push_back(std::move(directCall));

    const IrProgram boundIr = lowerToIr(program, boundSemantic);
    const IrValue *boundCall = boundIr.value(
        boundIr.instructions.front().expressionRoots.front());
    expect(boundCall != nullptr && boundCall->opcode == IrValueOpcode::Call &&
               boundCall->symbolId == 7,
           "sourceExprId-keyed semantic call binding produces a direct IR call");
    if (boundCall != nullptr && !boundCall->operands.empty()) {
        const IrValue *boundCallee = boundIr.value(boundCall->operands.front());
        expect(boundCallee != nullptr && boundCallee->symbolId == 7,
               "sourceExprId-keyed name binding is retained on the callee value");
    }
}

void testRecursiveStatementsKeepOneLegacyTokenOwner() {
    using namespace vietvm::frontend;
    using namespace vietvm::compiler;

    AstProgram program;
    for (const char *lexeme : {"{", "in", "1", ";", ";", "}"}) {
        program.tokens.push_back(token(lexeme));
    }
    const ExprId one = appendExpression(program, integerExpression("1"));

    AstStatement printed;
    printed.kind = AstStatementKind::Print;
    printed.tokenBegin = 1;
    printed.tokenEnd = 4;
    printed.expressionRoots.push_back(one);

    AstStatement empty;
    empty.kind = AstStatementKind::Empty;
    empty.tokenBegin = 4;
    empty.tokenEnd = 5;

    AstStatement block;
    block.kind = AstStatementKind::Block;
    block.tokenEnd = program.tokens.size();
    block.children.push_back(std::move(printed));
    block.children.push_back(std::move(empty));
    program.statements.push_back(std::move(block));

    IrProgram ir = lowerToIr(program, {});
    expect(ir.instructions.size() == 1 &&
               ir.instructions.front().opcode == IrOpcode::Block &&
               ir.instructions.front().children.size() == 2,
           "statement lowering preserves recursive block children");
    if (ir.instructions.empty()) return;

    const IrInstruction &loweredBlock = ir.instructions.front();
    expect(loweredBlock.tokens.size() == program.tokens.size(),
           "only the top-level IR statement owns the complete legacy token slice");
    bool childrenOwnNoTokens = true;
    for (const IrInstruction &child : loweredBlock.children) {
        childrenOwnNoTokens = childrenOwnNoTokens && child.tokens.empty();
    }
    expect(childrenOwnNoTokens,
           "recursive child IR does not duplicate compatibility tokens");
    expect(materializeIrTokens(ir) == tokenLexemes(program.tokens),
           "materialization emits every top-level source token exactly once");

    const OptimizationReport report = optimizeIr(ir);
    expect(report.removedNoOps == 1 &&
               ir.instructions.front().children.size() == 1,
           "IR optimization removes no-ops recursively");
    expect(materializeIrTokens(ir) == tokenLexemes(program.tokens),
           "recursive optimization cannot erase tokens owned by a fallback parent");
}

void testFunctionParameterDefaultsKeepParameterIndices() {
    using namespace vietvm::frontend;
    using namespace vietvm::compiler;

    AstProgram program;
    for (const char *lexeme : {"hàm", "f", "(", "a", ",", "b", "=", "2",
                               ")", "{", "}"}) {
        program.tokens.push_back(token(lexeme));
    }
    const ExprId two = appendExpression(program, integerExpression("2"));

    AstParameter first;
    first.name = "a";
    AstParameter second;
    second.name = "b";
    second.defaultValue = two;

    AstStatement body;
    body.kind = AstStatementKind::Block;
    body.tokenBegin = 9;
    body.tokenEnd = 11;

    AstStatement function;
    function.kind = AstStatementKind::Function;
    function.declarationName = "f";
    function.tokenEnd = program.tokens.size();
    function.parameters.push_back(std::move(first));
    function.parameters.push_back(std::move(second));
    function.children.push_back(std::move(body));
    program.statements.push_back(std::move(function));

    const IrProgram ir = lowerToIr(program, {});
    expect(ir.instructions.size() == 1 &&
               ir.instructions.front().parameterDefaults.size() == 2,
           "IR declaration keeps one default slot for every source parameter");
    if (ir.instructions.empty() ||
        ir.instructions.front().parameterDefaults.size() != 2) {
        return;
    }

    const std::vector<IrValueId> &defaults =
        ir.instructions.front().parameterDefaults;
    expect(defaults[0] == kInvalidIrValueId,
           "parameter without a default preserves its source index with a sentinel");
    const IrValue *defaultValue = ir.value(defaults[1]);
    expect(defaultValue != nullptr &&
               defaultValue->opcode == IrValueOpcode::ConstInt &&
               defaultValue->sourceExprId == two,
           "parameter default recursively lowers through its source expression ID");
    expect(ir.instructions.front().legacyRegion && ir.legacyRegionCount == 1,
           "function header remains one explicit fallback region during migration");
}

void testUnsupportedExpressionsAreExplicitLegacyRegions() {
    using namespace vietvm::frontend;
    using namespace vietvm::compiler;

    AstProgram program;
    for (const char *lexeme : {"hàm", "(", ")", "{", "}", ";",
                               "{", "\"k\"", ":", "1", "}", ";"}) {
        program.tokens.push_back(token(lexeme));
    }

    AstExpression lambda;
    lambda.kind = AstExpressionKind::Lambda;
    lambda.bodyTokenBegin = 3;
    lambda.bodyTokenEnd = 5;
    const ExprId lambdaId = appendExpression(program, std::move(lambda));

    AstExpression key;
    key.kind = AstExpressionKind::Literal;
    key.literalKind = AstLiteralKind::String;
    key.text = "\"k\"";
    const ExprId keyId = appendExpression(program, std::move(key));
    const ExprId valueId = appendExpression(program, integerExpression("1"));

    AstExpression map;
    map.kind = AstExpressionKind::MapLiteral;
    map.mapEntries.push_back({keyId, valueId, {}});
    const ExprId mapId = appendExpression(program, std::move(map));

    AstStatement lambdaStatement;
    lambdaStatement.kind = AstStatementKind::Expression;
    lambdaStatement.tokenEnd = 6;
    lambdaStatement.expressionRoots.push_back(lambdaId);
    program.statements.push_back(std::move(lambdaStatement));

    AstStatement mapStatement;
    mapStatement.kind = AstStatementKind::Expression;
    mapStatement.tokenBegin = 6;
    mapStatement.tokenEnd = program.tokens.size();
    mapStatement.expressionRoots.push_back(mapId);
    program.statements.push_back(std::move(mapStatement));

    IrProgram ir = lowerToIr(program, {});
    expect(ir.instructions.size() == 2,
           "unsupported value kinds still keep their containing statements");
    if (ir.instructions.size() != 2) return;

    const IrValue *lambdaValue = ir.value(ir.instructions[0].expressionRoots[0]);
    const IrValue *mapValue = ir.value(ir.instructions[1].expressionRoots[0]);
    expect(lambdaValue != nullptr && lambdaValue->opcode == IrValueOpcode::LegacyRegion,
           "lambda with token-only body is explicitly marked as a legacy region");
    expect(mapValue != nullptr && mapValue->opcode == IrValueOpcode::MapLiteral &&
               mapValue->operands.size() == 2,
           "map literal has a structured opcode and retains its recursive entry graph");
    expect(ir.legacyRegionCount == 1,
           "program exposes a deterministic count of explicit fallback nodes");

    if (lambdaValue != nullptr) {
        ir.values[lambdaValue->id].opcode = IrValueOpcode::LoadName;
    }
    expect(recomputeLegacyRegionCount(ir) == 0 && ir.legacyRegionCount == 0,
           "legacy-region count is recomputed after an IR rewrite instead of becoming stale");

    IrValue orphan;
    orphan.id = ir.values.size();
    orphan.opcode = IrValueOpcode::LegacyRegion;
    ir.values.push_back(std::move(orphan));
    expect(recomputeLegacyRegionCount(ir) == 0,
           "fallback accounting ignores value-arena nodes no longer reachable from statements");
}

} // namespace

int main() {
    testRecursiveExpressionLowering();
    testRecursiveStatementsKeepOneLegacyTokenOwner();
    testFunctionParameterDefaultsKeepParameterIndices();
    testUnsupportedExpressionsAreExplicitLegacyRegions();

    if (failures != 0) {
        std::cerr << failures << " recursive IR unit test(s) failed\n";
        return 1;
    }

    std::cout << "recursive IR unit tests passed\n";
    return 0;
}
