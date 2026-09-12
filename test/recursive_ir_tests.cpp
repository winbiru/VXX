#include <iostream>
#include <string>
#include <vector>

#include "frontend/lexer.h"
#include "vpp/compiler/ir.h"
#include "vpp/compiler/optimizer.h"
#include "vpp/frontend/parser.h"

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

vietvm::frontend::AstProgram parseSource(const std::string &source) {
    return vietvm::frontend::parseTokens(
        vietvm::compiler::postProcessTokensWithSpans(
            vietvm::compiler::tokenizeWithSpans(source)));
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
    second.hasDefault = true;
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
               ir.instructions.front().parameters.size() == 2,
           "IR declaration keeps structured metadata for every source parameter");
    if (ir.instructions.empty() ||
        ir.instructions.front().parameters.size() != 2) {
        return;
    }

    const std::vector<IrParameter> &parameters =
        ir.instructions.front().parameters;
    expect(!parameters[0].hasDefault &&
               parameters[0].defaultValue == kInvalidIrValueId,
           "parameter without a default preserves its source index with a sentinel");
    const IrValue *defaultValue = ir.value(parameters[1].defaultValue);
    expect(defaultValue != nullptr &&
               defaultValue->opcode == IrValueOpcode::ConstInt &&
               defaultValue->sourceExprId == two,
           "parameter default recursively lowers through its source expression ID");
    expect(ir.instructions.front().declarationName == "f" &&
               parameters[1].hasDefault &&
               !ir.instructions.front().legacyRegion && ir.legacyRegionCount == 0,
           "a fully structured function header no longer needs a token fallback region");
}

void testStructuredSwitchMetadataLowersRecursively() {
    using namespace vietvm::frontend;
    using namespace vietvm::compiler;

    AstProgram program;
    for (const char *lexeme : {"chọn", "(", "x", ")", "{", "ca", "key", ":",
                               "{", "thoát", ";", "}", "mặc định", ":", "{", "}", "}"}) {
        program.tokens.push_back(token(lexeme));
    }
    const ExprId selector = appendExpression(program, nameExpression("x"));
    const ExprId label = appendExpression(program, nameExpression("key"));

    AstStatement breakStatement;
    breakStatement.kind = AstStatementKind::Break;
    breakStatement.tokenBegin = 9;
    breakStatement.tokenEnd = 11;

    AstStatement caseBody;
    caseBody.kind = AstStatementKind::Block;
    caseBody.tokenBegin = 8;
    caseBody.tokenEnd = 12;
    caseBody.children.push_back(std::move(breakStatement));

    AstStatement defaultBody;
    defaultBody.kind = AstStatementKind::Block;
    defaultBody.tokenBegin = 14;
    defaultBody.tokenEnd = 16;

    AstSwitchArm caseArm;
    caseArm.kind = AstSwitchArmKind::Case;
    caseArm.label = label;
    caseArm.bodyChildIndex = 0;
    caseArm.hasColon = true;
    caseArm.prefixedByCase = true;

    AstSwitchArm defaultArm;
    defaultArm.kind = AstSwitchArmKind::Default;
    defaultArm.bodyChildIndex = 1;
    defaultArm.hasColon = true;

    AstStatement selection;
    selection.kind = AstStatementKind::Switch;
    selection.switchForm = AstSwitchForm::Structured;
    selection.tokenEnd = program.tokens.size();
    selection.expressionRoots = {selector, label};
    selection.children.push_back(std::move(caseBody));
    selection.children.push_back(std::move(defaultBody));
    selection.switchArms.push_back(std::move(caseArm));
    selection.switchArms.push_back(std::move(defaultArm));
    program.statements.push_back(std::move(selection));

    SemanticModel semantic;
    BindingResult labelBinding;
    labelBinding.expression = label;
    labelBinding.kind = BindingKind::Symbol;
    labelBinding.symbol = 4;
    semantic.expressionBindings.push_back(std::move(labelBinding));

    const IrProgram ir = lowerToIr(program, semantic);
    expect(ir.instructions.size() == 1 &&
               ir.instructions.front().opcode == IrOpcode::Switch &&
               ir.instructions.front().switchForm == AstSwitchForm::Structured &&
               ir.instructions.front().switchArms.size() == 2 &&
               ir.instructions.front().children.size() == 2,
           "structured switch arms lower alongside indexed block children");
    if (ir.instructions.empty() || ir.instructions.front().switchArms.size() != 2) return;

    const IrInstruction &lowered = ir.instructions.front();
    const IrSwitchArm &loweredCase = lowered.switchArms[0];
    const IrSwitchArm &loweredDefault = lowered.switchArms[1];
    const IrValue *labelValue = ir.value(loweredCase.label);
    expect(labelValue != nullptr && labelValue->opcode == IrValueOpcode::LoadName &&
               labelValue->sourceExprId == label && labelValue->symbolId == 4,
           "switch case label lowers through its semantic expression identity");
    expect(loweredDefault.kind == AstSwitchArmKind::Default &&
               loweredDefault.label == kInvalidIrValueId &&
               loweredCase.bodyChildIndex == 0 && loweredDefault.bodyChildIndex == 1,
           "default sentinel and arm-to-child indices survive IR lowering");
    expect(lowered.children[0].children.size() == 1 &&
               lowered.children[0].children.front().opcode == IrOpcode::Break,
           "case body statements are recursively lowered rather than token-reparsed");
    expect(!lowered.legacyRegion && ir.legacyRegionCount == 0,
           "fully structured switch syntax no longer marks a statement fallback region");
    expect(lowered.tokens.size() == program.tokens.size() &&
               lowered.children[0].tokens.empty() && lowered.children[1].tokens.empty(),
           "only the top-level switch owns compatibility tokens");
}

void testStructuredTryMetadataLowersRecursively() {
    using namespace vietvm::frontend;
    using namespace vietvm::compiler;

    AstProgram program;
    for (const char *lexeme : {"thử", "{", "ném", "1", ";", "}",
                               "bắt lỗi", "(", "e", ")", "{", "in",
                               "e", ";", "}"}) {
        program.tokens.push_back(token(lexeme));
    }
    const ExprId thrownValue = appendExpression(program, integerExpression("1"));
    const ExprId caughtValue = appendExpression(program, nameExpression("e"));

    AstStatement thrown;
    thrown.kind = AstStatementKind::Throw;
    thrown.tokenBegin = 2;
    thrown.tokenEnd = 5;
    thrown.expressionRoots.push_back(thrownValue);

    AstStatement tryBody;
    tryBody.kind = AstStatementKind::Block;
    tryBody.tokenBegin = 1;
    tryBody.tokenEnd = 6;
    tryBody.children.push_back(std::move(thrown));

    AstStatement printed;
    printed.kind = AstStatementKind::Print;
    printed.tokenBegin = 11;
    printed.tokenEnd = 14;
    printed.expressionRoots.push_back(caughtValue);

    AstStatement catchBody;
    catchBody.kind = AstStatementKind::Block;
    catchBody.tokenBegin = 10;
    catchBody.tokenEnd = 15;
    catchBody.children.push_back(std::move(printed));

    AstStatement guarded;
    guarded.kind = AstStatementKind::Try;
    guarded.tryForm = AstTryForm::TryCatchBlocks;
    guarded.catchVariable = "e";
    guarded.tokenEnd = program.tokens.size();
    guarded.children.push_back(std::move(tryBody));
    guarded.children.push_back(std::move(catchBody));
    program.statements.push_back(std::move(guarded));

    SemanticModel semantic;
    semantic.globalScope = 0;
    SemanticScope globalScope;
    globalScope.id = 0;
    globalScope.kind = ScopeKind::Global;
    SemanticScope catchScope;
    catchScope.id = 1;
    catchScope.kind = ScopeKind::Catch;
    catchScope.parent = 0;
    SemanticScope catchBlockScope;
    catchBlockScope.id = 2;
    catchBlockScope.kind = ScopeKind::Block;
    catchBlockScope.parent = 1;
    semantic.scopes = {globalScope, catchScope, catchBlockScope};
    semantic.statementScopes[10] = 2;

    SemanticSymbol catchSymbol;
    catchSymbol.id = 0;
    catchSymbol.kind = SemanticSymbolKind::CatchVariable;
    catchSymbol.name = "e";
    catchSymbol.lookupName = "e";
    catchSymbol.declaringScope = 1;
    semantic.symbols.push_back(std::move(catchSymbol));

    BindingResult caughtBinding;
    caughtBinding.expression = caughtValue;
    caughtBinding.kind = BindingKind::Symbol;
    caughtBinding.symbol = 0;
    semantic.expressionBindings.push_back(std::move(caughtBinding));

    const IrProgram ir = lowerToIr(program, semantic);
    expect(ir.instructions.size() == 1 &&
               ir.instructions.front().opcode == IrOpcode::Try &&
               ir.instructions.front().tryForm == AstTryForm::TryCatchBlocks &&
               ir.instructions.front().catchVariable == "e" &&
               ir.instructions.front().catchSymbolId == 0,
           "IR carries structured try form and resolved catch SymbolId");
    if (ir.instructions.empty()) return;

    const IrInstruction &lowered = ir.instructions.front();
    expect(lowered.children.size() == 2 &&
               lowered.children[0].opcode == IrOpcode::Block &&
               lowered.children[1].opcode == IrOpcode::Block,
           "try and catch bodies lower as two recursive IR blocks");
    if (lowered.children.size() == 2) {
        expect(lowered.children[0].children.size() == 1 &&
                   lowered.children[0].children.front().opcode == IrOpcode::Throw &&
                   lowered.children[1].children.size() == 1 &&
                   lowered.children[1].children.front().opcode == IrOpcode::Print,
               "nested try/catch statements lower recursively without reparsing tokens");
    }
    const IrValue *caught = ir.value(
        lowered.children.size() < 2 || lowered.children[1].children.empty() ||
                lowered.children[1].children.front().expressionRoots.empty()
            ? kInvalidIrValueId
            : lowered.children[1].children.front().expressionRoots.front());
    expect(caught != nullptr && caught->opcode == IrValueOpcode::LoadName &&
               caught->symbolId == 0,
           "catch-body IR value retains the same resolved catch SymbolId");
    expect(!lowered.legacyRegion && ir.legacyRegionCount == 0 &&
               lowered.tokens.size() == program.tokens.size() &&
               lowered.children[0].tokens.empty() &&
               lowered.children[1].tokens.empty(),
           "structured try IR is direct-ready while only its root owns compatibility tokens");
}

void testStructuredClassMetadataLowersQualifiedMethods() {
    using namespace vietvm::frontend;
    using namespace vietvm::compiler;

    AstProgram program;
    for (const char *lexeme : {"lớp", "Toan", "{", "hàm", "cộng", "(", ")",
                               "{", "}", "}"}) {
        program.tokens.push_back(token(lexeme));
    }

    AstStatement methodBody;
    methodBody.kind = AstStatementKind::Block;
    methodBody.tokenBegin = 7;
    methodBody.tokenEnd = 9;

    AstStatement method;
    method.kind = AstStatementKind::Function;
    method.tokenBegin = 3;
    method.tokenEnd = 9;
    method.declarationName = "cộng";
    method.visibility = AstVisibility::Protected;
    method.children.push_back(std::move(methodBody));

    AstStatement classBody;
    classBody.kind = AstStatementKind::Block;
    classBody.tokenBegin = 2;
    classBody.tokenEnd = 10;
    classBody.children.push_back(std::move(method));

    AstStatement klass;
    klass.kind = AstStatementKind::Class;
    klass.tokenEnd = program.tokens.size();
    klass.declarationName = "Toan";
    klass.visibility = AstVisibility::Public;
    klass.classForm = AstClassForm::MethodBlock;
    klass.children.push_back(std::move(classBody));
    program.statements.push_back(std::move(klass));

    SemanticModel semantic;
    SemanticSymbol classSymbol;
    classSymbol.id = 0;
    classSymbol.kind = SemanticSymbolKind::Class;
    classSymbol.lookupName = "Toan";
    classSymbol.qualifiedName = "Toan";
    classSymbol.visibility = SemanticVisibility::Public;
    semantic.symbols.push_back(std::move(classSymbol));

    SemanticSymbol methodSymbol;
    methodSymbol.id = 1;
    methodSymbol.kind = SemanticSymbolKind::Method;
    methodSymbol.lookupName = "cộng";
    methodSymbol.qualifiedName = "Toan.cộng";
    methodSymbol.visibility = SemanticVisibility::Protected;
    methodSymbol.ownerClass = 0;
    semantic.symbols.push_back(std::move(methodSymbol));
    semantic.declarationSymbols[0] = 0;
    semantic.declarationSymbols[3] = 1;

    const IrProgram ir = lowerToIr(program, semantic);
    expect(ir.instructions.size() == 1 &&
               ir.instructions.front().opcode == IrOpcode::DefineClass &&
               ir.instructions.front().classForm == AstClassForm::MethodBlock &&
               !ir.instructions.front().legacyRegion,
           "structured class metadata lowers without a class-level fallback");
    if (ir.instructions.empty() || ir.instructions.front().children.empty() ||
        ir.instructions.front().children.front().children.empty()) {
        return;
    }

    const IrInstruction &loweredClass = ir.instructions.front();
    const IrInstruction &loweredMethod =
        loweredClass.children.front().children.front();
    expect(loweredClass.visibility == AstVisibility::Public &&
               loweredClass.effectiveVisibility == SemanticVisibility::Public &&
               loweredMethod.declarationName == "Toan.cộng" &&
               loweredMethod.visibility == AstVisibility::Protected &&
               loweredMethod.effectiveVisibility == SemanticVisibility::Protected &&
               loweredMethod.symbolId == 1,
           "class and method IR retain source/effective visibility and semantic identity");
    expect(!loweredMethod.legacyRegion && ir.legacyRegionCount == 0 &&
               loweredClass.tokens.size() == program.tokens.size() &&
               loweredClass.children.front().tokens.empty() &&
               loweredMethod.tokens.empty(),
           "structured class recursion keeps one lossless root token owner");
}

void testStructuredLambdasLowerBodiesDefaultsCapturesAndNestedIds() {
    using namespace vietvm::frontend;
    using namespace vietvm::compiler;

    const AstProgram program = parseSource(
        "hàm enclosing(p) {\n"
        "  local = 1;\n"
        "  factory = hàm(x, y = 2) {\n"
        "    nested = hàm(z) { trả về z + p; };\n"
        "    trả về x + y + local;\n"
        "  };\n"
        "}");
    const SemanticModel semantic = analyzeSemantics(program);
    IrProgram ir = lowerToIr(program, semantic);

    expect(ir.lambdas.size() == 2,
           "recursive IR owns one stable payload for each nested lambda");
    for (const IrLambda &lambda : ir.lambdas) {
        const IrValue *owner = ir.value(lambda.ownerValue);
        expect(lambda.id < ir.lambdas.size() && ir.lambda(lambda.id) == &lambda &&
                   owner != nullptr && owner->opcode == IrValueOpcode::Lambda &&
                   owner->lambdaId == lambda.id &&
                   owner->sourceExprId == lambda.sourceExprId,
               "IrLambda ID, owner value and source ExprId form a stable bidirectional mapping");
    }

    const IrLambda *outer = nullptr;
    const IrLambda *inner = nullptr;
    for (const IrLambda &lambda : ir.lambdas) {
        if (!lambda.parameters.empty() && lambda.parameters.front().name == "x") {
            outer = &lambda;
        } else if (!lambda.parameters.empty() &&
                   lambda.parameters.front().name == "z") {
            inner = &lambda;
        }
    }
    expect(outer != nullptr && inner != nullptr,
           "nested lowering preserves each lambda's structured parameter list");
    if (outer == nullptr || inner == nullptr) return;

    expect(outer->parameters.size() == 2 &&
               outer->parameters[0].symbolId >= 0 &&
               outer->parameters[1].symbolId >= 0 &&
               outer->parameters[1].hasDefault,
           "lambda IR parameters retain semantic SymbolIds and default presence");
    if (outer->parameters.size() == 2) {
        const IrValue *defaultValue = ir.value(outer->parameters[1].defaultValue);
        expect(defaultValue != nullptr &&
                   defaultValue->opcode == IrValueOpcode::ConstInt &&
                   defaultValue->text == "2",
               "lambda defaults recursively lower into the IR value arena");
    }
    expect(outer->body.opcode == IrOpcode::Block &&
               outer->body.children.size() == 2 &&
               outer->body.children.front().opcode == IrOpcode::Statement &&
               outer->body.children.back().opcode == IrOpcode::Return,
           "lambda body recursively lowers as an ordinary IR block tree");

    const SemanticLambda *outerSemantic =
        semantic.lambdaForExpression(outer->sourceExprId);
    const SemanticLambda *innerSemantic =
        semantic.lambdaForExpression(inner->sourceExprId);
    expect(outerSemantic != nullptr && innerSemantic != nullptr &&
               outer->captures.size() == outerSemantic->captures.size() &&
               inner->captures.size() == innerSemantic->captures.size(),
           "lambda IR carries the semantic capture set without token recovery");
    expect(ir.legacyRegionCount == 0,
           "fully structured nested lambda trees contain no fallback regions");

    const std::vector<std::string> materialized = materializeIrTokens(ir);
    expect(materialized.size() == program.tokens.size(),
           "lambda recursion does not duplicate top-level compatibility tokens");
    for (std::size_t index = 0;
         index < materialized.size() && index < program.tokens.size(); ++index) {
        expect(materialized[index] == program.tokens[index].lexeme,
               "lambda token materialization preserves the frozen source token sequence");
    }
}

void testLambdaBodyImportAccountingAndOptimizationAreRecursive() {
    using namespace vietvm::frontend;
    using namespace vietvm::compiler;

    const AstProgram program = parseSource(
        "handler = hàm() { ; nhập package; };\n");
    const SemanticModel semantic = analyzeSemantics(program);
    IrProgram ir = lowerToIr(program, semantic);
    expect(ir.lambdas.size() == 1 && ir.legacyRegionCount == 0,
           "structured import inside a lambda body no longer creates a legacy fallback");
    if (ir.lambdas.empty()) return;

    const OptimizationReport report = optimizeIr(ir);
    expect(report.removedNoOps == 1 &&
               ir.lambdas.front().body.children.size() == 1 &&
               ir.lambdas.front().body.children.front().opcode == IrOpcode::Import,
           "IR optimization recursively removes no-op statements inside lambda bodies");
    expect(ir.legacyRegionCount == 0,
           "recursive optimization preserves zero fallback for a structured lambda import");
    ir.lambdas.front().body.children.front().legacyRegion = true;
    expect(recomputeLegacyRegionCount(ir) == 1,
           "recomputed fallback accounting still reaches rewritten lambda-body instructions");
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
           "lambda without an arena-owned payload is explicitly marked as a legacy region");
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
    testStructuredSwitchMetadataLowersRecursively();
    testStructuredTryMetadataLowersRecursively();
    testStructuredClassMetadataLowersQualifiedMethods();
    testStructuredLambdasLowerBodiesDefaultsCapturesAndNestedIds();
    testLambdaBodyImportAccountingAndOptimizationAreRecursive();
    testUnsupportedExpressionsAreExplicitLegacyRegions();

    if (failures != 0) {
        std::cerr << failures << " recursive IR unit test(s) failed\n";
        return 1;
    }

    std::cout << "recursive IR unit tests passed\n";
    return 0;
}
