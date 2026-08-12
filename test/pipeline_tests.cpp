#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "frontend/keywords.h"
#include "frontend/lexer.h"
#include "vpp/compiler/compiler.h"
#include "vpp/compiler/ir.h"
#include "vpp/compiler/optimizer.h"
#include "vpp/compiler/pipeline.h"
#include "vpp/compiler/semantic.h"
#include "vpp/frontend/parser.h"

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<vietvm::frontend::Token> lexSource(const std::string &source) {
    return vietvm::compiler::postProcessTokensWithSpans(
        vietvm::compiler::tokenizeWithSpans(source));
}

vietvm::frontend::AstProgram parseSource(const std::string &source) {
    return vietvm::frontend::parseTokens(lexSource(source));
}

const vietvm::frontend::AstStatement *findFunction(
    const vietvm::frontend::AstProgram &program,
    const std::string &name) {
    for (const vietvm::frontend::AstStatement &statement : program.statements) {
        if (statement.kind == vietvm::frontend::AstStatementKind::Function &&
            statement.declarationName == name) {
            return &statement;
        }
    }
    return nullptr;
}

const vietvm::compiler::SemanticReference *findReference(
    const vietvm::compiler::SemanticModel &model,
    const std::string &name) {
    for (const vietvm::compiler::SemanticReference &reference : model.references) {
        if (reference.name == name) return &reference;
    }
    return nullptr;
}

const vietvm::frontend::AstExpression *findExpression(
    const vietvm::frontend::AstProgram &program,
    vietvm::frontend::ExprId id) {
    return program.expression(id);
}

const vietvm::compiler::SemanticSymbol *findSymbol(
    const vietvm::compiler::SemanticModel &model,
    vietvm::compiler::SemanticSymbolKind kind,
    const std::string &lookupName) {
    for (const auto &symbol : model.symbols) {
        if (symbol.kind == kind && symbol.lookupName == lookupName) return &symbol;
    }
    return nullptr;
}

const vietvm::frontend::AstExpression *findCallByCallee(
    const vietvm::frontend::AstProgram &program,
    const std::string &calleeName) {
    for (const auto &expression : program.expressions) {
        if (expression.kind != vietvm::frontend::AstExpressionKind::Call) continue;
        const auto *callee = program.expression(expression.callee);
        if (callee != nullptr && callee->kind == vietvm::frontend::AstExpressionKind::Name &&
            callee->text == calleeName) {
            return &expression;
        }
    }
    return nullptr;
}

void testSpanCarryingTokensAndMultiwordKeyword() {
    const std::string source = "trả về;";
    const std::vector<vietvm::frontend::Token> raw =
        vietvm::compiler::tokenizeWithSpans(source);

    expect(raw.size() == 3, "span lexer keeps the raw multi-word keyword pieces");
    if (raw.size() == 3) {
        expect(raw[0].lexeme == "trả" && raw[1].lexeme == "về",
               "raw span lexer preserves source spellings");
        expect(raw[0].span.begin.offset == 0 && raw[0].span.begin.line == 1 &&
                   raw[0].span.begin.column == 1,
               "raw token carries its source start position");
        expect(raw[1].span.end.offset == source.find(';'),
               "raw token carries its byte-oriented source end position");
    }

    const std::vector<vietvm::frontend::Token> processed =
        vietvm::compiler::postProcessTokensWithSpans(raw);
    expect(processed.size() == 2,
           "post-processing merges a multi-word keyword without dropping punctuation");
    if (processed.size() == 2 && raw.size() == 3) {
        const vietvm::frontend::Token &merged = processed.front();
        expect(merged.lexeme == "trả về" &&
                   merged.kind == vietvm::frontend::TokenKind::Keyword,
               "post-processing emits the canonical multi-word keyword token");
        expect(merged.span.begin.offset == raw[0].span.begin.offset &&
                   merged.span.end.offset == raw[1].span.end.offset,
               "merged keyword span covers every original keyword piece");
        expect(merged.span.end.line == 1 &&
                   merged.span.end.column == source.find(';') + 1,
               "merged keyword retains the correct source range");
    }
}

void testParserBuildsFunctionAstWithNestedReturnAndRange() {
    const std::string source = "hàm cộng một(a) {\n  trả về a;\n}";
    const vietvm::frontend::AstProgram program = parseSource(source);

    expect(program.statements.size() == 1,
           "parser creates one top-level statement for one function declaration");
    const vietvm::frontend::AstStatement *function = findFunction(program, "cộng một");
    expect(function != nullptr, "parser identifies the multi-word function declaration name");
    if (function == nullptr) return;

    expect(function->span.begin.offset == 0 && function->span.begin.line == 1 &&
               function->span.begin.column == 1 && function->span.end.offset == source.size(),
           "function AST node spans its complete source declaration");
    expect(function->children.size() == 1 &&
               function->children.front().kind == vietvm::frontend::AstStatementKind::Block,
           "function AST node retains its brace-delimited body block");
    if (function->children.size() != 1) return;

    const vietvm::frontend::AstStatement &body = function->children.front();
    expect(body.children.size() == 1 &&
               body.children.front().kind == vietvm::frontend::AstStatementKind::Return,
           "function body contains the nested return statement");
    if (body.children.size() != 1) return;

    const vietvm::frontend::AstStatement &returned = body.children.front();
    expect(returned.span.begin.offset == source.find("trả") &&
               returned.span.begin.line == 2 && returned.span.begin.column == 3 &&
               returned.span.end.offset == source.find(';') + 1,
           "nested return AST node preserves its exact source range");
}

void testParserReportsStructuralErrorsWithSpans() {
    bool rejected = false;
    try {
        (void)parseSource("hàm main() { trả về 1;");
    } catch (const vietvm::frontend::ParseError &error) {
        rejected = true;
        expect(error.span.begin.line == 1 && error.span.begin.column > 1,
               "parser error retains the source span of the unclosed block");
    }
    expect(rejected, "parser rejects an unclosed block before semantic analysis");
}

void testParserReportsUnmatchedClosingBracket() {
    const std::string source = "in danh_sách[0]];";
    const std::size_t unmatchedOffset = source.rfind(']');
    bool rejected = false;
    try {
        (void)parseSource(source);
    } catch (const vietvm::frontend::ParseError &error) {
        rejected = true;
        expect(std::string(error.what()).find("[VPP-SYN-035]") != std::string::npos,
               "unmatched ']' uses the stable square-bracket diagnostic code");
        expect(error.span.begin.offset == unmatchedOffset &&
                   error.span.end.offset == unmatchedOffset + 1 &&
                   error.span.begin.line == 1 &&
                   error.span.begin.column == unmatchedOffset + 1,
               "unmatched ']' diagnostic points at the extra closing bracket");
    }
    expect(rejected, "parser rejects an unmatched closing square bracket");
}

void testParserReportsUnclosedOpeningBracket() {
    const std::string source = "in danh_sách[0;";
    bool rejected = false;
    try {
        (void)parseSource(source);
    } catch (const vietvm::frontend::ParseError &error) {
        rejected = true;
        expect(std::string(error.what()).find("[VPP-SYN-035]") != std::string::npos,
               "unclosed '[' uses the stable square-bracket diagnostic code");
        expect(error.span.begin.offset == 0 && error.span.end.offset == source.size() &&
                   error.span.begin.line == 1 && error.span.begin.column == 1,
               "unclosed '[' diagnostic spans the incomplete statement");
    }
    expect(rejected, "parser rejects an unclosed opening square bracket");
}

void testParserRejectsCrossedDelimiterNesting() {
    const std::string source = "in ([1)];";
    const std::size_t closingOffset = source.find(')');
    bool rejected = false;
    try {
        (void)parseSource(source);
    } catch (const vietvm::frontend::ParseError &error) {
        rejected = true;
        expect(std::string(error.what()).find("[VPP-SYN-002]") != std::string::npos,
               "crossed parenthesis/bracket nesting reports the closing delimiter family");
        expect(error.span.begin.offset == closingOffset &&
                   error.span.end.offset == closingOffset + 1,
               "crossed delimiter diagnostic points at the first invalid closer");
    }
    expect(rejected, "parser rejects delimiters that balance numerically but cross nesting order");

    rejected = false;
    try {
        (void)parseSource("in ([1;");
    } catch (const vietvm::frontend::ParseError &error) {
        rejected = true;
        expect(std::string(error.what()).find("[VPP-SYN-035]") != std::string::npos,
               "EOF reports the innermost unclosed delimiter from the nesting stack");
    }
    expect(rejected, "parser rejects multiple unclosed delimiter families at EOF");
}

void testExpressionAstUsesStableIdsAndLegacyPrecedence() {
    const std::string source = "kết quả = -a + b * 2 == 9 || sai;";
    const vietvm::frontend::AstProgram program = parseSource(source);
    expect(program.statements.size() == 1 &&
               program.statements.front().expressionRoots.size() == 1,
           "expression statement receives one committed expression root");
    if (program.statements.empty() || program.statements.front().expressionRoots.empty()) return;

    for (std::size_t index = 0; index < program.expressions.size(); ++index) {
        expect(program.expressions[index].id == index,
               "expression arena IDs remain equal to their stable indices");
    }

    const auto *assignment = findExpression(
        program, program.statements.front().expressionRoots.front());
    expect(assignment != nullptr &&
               assignment->kind == vietvm::frontend::AstExpressionKind::Assignment &&
               assignment->text == "=",
           "lowest-precedence assignment becomes the expression root");
    if (assignment == nullptr) return;
    expect(assignment->span.begin.offset == 0 &&
               assignment->span.end.offset == source.find(';'),
           "expression root span excludes only the statement terminator");

    const auto *logical = findExpression(program, assignment->right);
    const auto *equality = logical == nullptr ? nullptr : findExpression(program, logical->left);
    const auto *addition = equality == nullptr ? nullptr : findExpression(program, equality->left);
    const auto *product = addition == nullptr ? nullptr : findExpression(program, addition->right);
    const auto *negative = addition == nullptr ? nullptr : findExpression(program, addition->left);
    expect(logical != nullptr && logical->kind == vietvm::frontend::AstExpressionKind::Binary &&
               logical->text == "||",
           "logical OR binds below equality");
    expect(equality != nullptr && equality->text == "==",
           "equality binds below arithmetic");
    expect(addition != nullptr && addition->text == "+" &&
               product != nullptr && product->text == "*",
           "multiplication binds more tightly than addition");
    expect(negative != nullptr &&
               negative->kind == vietvm::frontend::AstExpressionKind::Unary &&
               negative->text == "-",
           "prefix negation is represented explicitly");

    const auto *booleanLiteral = logical == nullptr
        ? nullptr
        : findExpression(program, logical->right);
    expect(booleanLiteral != nullptr &&
               booleanLiteral->literalKind == vietvm::frontend::AstLiteralKind::Boolean,
           "literal category records booleans without reinterpreting their text");
}

void testExpressionAstCallsAssignmentsAndPostfix() {
    const vietvm::frontend::AstProgram program = parseSource(
        "tổng += mạng gửi dữ liệu(1, x + 2);\n"
        "x--;\n"
        "gọi tác vụ nền();\n"
        "in yêu cầu đúng(đúng, \"message\");");
    expect(program.statements.size() == 4,
           "four expression-bearing statements remain separate after parsing");
    if (program.statements.size() != 4) return;

    const auto *compound = program.statements[0].expressionRoots.empty()
        ? nullptr
        : findExpression(program, program.statements[0].expressionRoots.front());
    const auto *call = compound == nullptr ? nullptr : findExpression(program, compound->right);
    const auto *callee = call == nullptr ? nullptr : findExpression(program, call->callee);
    expect(compound != nullptr &&
               compound->kind == vietvm::frontend::AstExpressionKind::CompoundAssignment &&
               compound->text == "+=",
           "compound assignment has a dedicated AST kind");
    expect(call != nullptr && call->kind == vietvm::frontend::AstExpressionKind::Call &&
               call->arguments.size() == 2 && callee != nullptr &&
               callee->kind == vietvm::frontend::AstExpressionKind::Name &&
               callee->text == "mạng gửi dữ liệu",
           "call AST retains a maximal multiword callable name and argument roots");

    const auto *postfix = program.statements[1].expressionRoots.empty()
        ? nullptr
        : findExpression(program, program.statements[1].expressionRoots.front());
    expect(postfix != nullptr &&
               postfix->kind == vietvm::frontend::AstExpressionKind::Postfix &&
               postfix->text == "--",
           "postfix decrement has a dedicated AST node");

    const auto *explicitCall = program.statements[2].expressionRoots.empty()
        ? nullptr
        : findExpression(program, program.statements[2].expressionRoots.front());
    const auto *explicitCallee = explicitCall == nullptr
        ? nullptr
        : findExpression(program, explicitCall->callee);
    expect(explicitCall != nullptr &&
               explicitCall->kind == vietvm::frontend::AstExpressionKind::Call &&
               explicitCallee != nullptr && explicitCallee->text == "tác vụ nền",
           "the explicit 'gọi' form also produces a call expression");

    const auto *keywordCall = program.statements[3].expressionRoots.empty()
        ? nullptr
        : findExpression(program, program.statements[3].expressionRoots.front());
    const auto *keywordCallee = keywordCall == nullptr
        ? nullptr
        : findExpression(program, keywordCall->callee);
    expect(keywordCall != nullptr && keywordCallee != nullptr &&
               keywordCallee->text == "yêu cầu đúng",
           "literal keywords remain valid pieces of a contextual callable name");

    const vietvm::frontend::AstProgram associativity = parseSource("a = b = 1;");
    const auto *outer = associativity.statements.front().expressionRoots.empty()
        ? nullptr
        : findExpression(associativity,
                         associativity.statements.front().expressionRoots.front());
    const auto *inner = outer == nullptr ? nullptr : findExpression(associativity, outer->right);
    expect(outer != nullptr && outer->kind == vietvm::frontend::AstExpressionKind::Assignment &&
               inner != nullptr && inner->kind == vietvm::frontend::AstExpressionKind::Assignment,
           "simple assignment retains the legacy right associativity");

    const vietvm::frontend::AstProgram mixedLeft = parseSource("a = b += c;");
    const auto *mixedLeftRoot = mixedLeft.statements.front().expressionRoots.empty()
        ? nullptr
        : findExpression(mixedLeft, mixedLeft.statements.front().expressionRoots.front());
    const auto *mixedLeftChild = mixedLeftRoot == nullptr
        ? nullptr
        : findExpression(mixedLeft, mixedLeftRoot->left);
    expect(mixedLeftRoot != nullptr &&
               mixedLeftRoot->kind == vietvm::frontend::AstExpressionKind::CompoundAssignment &&
               mixedLeftRoot->text == "+=" && mixedLeftChild != nullptr &&
               mixedLeftChild->kind == vietvm::frontend::AstExpressionKind::Assignment,
           "a left-associative compound assignment pops a preceding plain assignment");

    const vietvm::frontend::AstProgram mixedRight = parseSource("a += b = c;");
    const auto *mixedRightRoot = mixedRight.statements.front().expressionRoots.empty()
        ? nullptr
        : findExpression(mixedRight, mixedRight.statements.front().expressionRoots.front());
    const auto *mixedRightChild = mixedRightRoot == nullptr
        ? nullptr
        : findExpression(mixedRight, mixedRightRoot->right);
    expect(mixedRightRoot != nullptr &&
               mixedRightRoot->kind == vietvm::frontend::AstExpressionKind::CompoundAssignment &&
               mixedRightChild != nullptr &&
               mixedRightChild->kind == vietvm::frontend::AstExpressionKind::Assignment,
           "a following right-associative plain assignment stays in the compound RHS");

    const vietvm::frontend::AstProgram keywordNamedCalls = parseSource(
        "in hàm();\n"
        "in gọi();");
    expect(keywordNamedCalls.statements.size() == 2 &&
               keywordNamedCalls.statements[0].expressionRoots.size() == 1 &&
               keywordNamedCalls.statements[1].expressionRoots.size() == 1,
           "lambda/call keywords remain usable as contextual zero-argument callable names");
    if (keywordNamedCalls.statements.size() == 2 &&
        !keywordNamedCalls.statements[0].expressionRoots.empty() &&
        !keywordNamedCalls.statements[1].expressionRoots.empty()) {
        const auto *hamCall = findExpression(
            keywordNamedCalls, keywordNamedCalls.statements[0].expressionRoots.front());
        const auto *goiCall = findExpression(
            keywordNamedCalls, keywordNamedCalls.statements[1].expressionRoots.front());
        const auto *hamName = hamCall == nullptr
            ? nullptr
            : findExpression(keywordNamedCalls, hamCall->callee);
        const auto *goiName = goiCall == nullptr
            ? nullptr
            : findExpression(keywordNamedCalls, goiCall->callee);
        expect(hamName != nullptr && hamName->text == "hàm" &&
                   goiName != nullptr && goiName->text == "gọi",
               "context disambiguates ordinary keyword-named calls from lambda/explicit-call syntax");
    }
}

void testExpressionAstLambdaMapAndLiteralCategories() {
    const vietvm::frontend::AstProgram program = parseSource(
        "handler = hàm(x, y = 2) { trả về x + y; };\n"
        "metadata = {\"count\": 1, active: đúng, empty: rỗng, "
        "ratio: 1.5, label: \"vpp\"};");
    expect(program.statements.size() == 2,
           "lambda and map assignments remain ordinary statements");
    if (program.statements.size() != 2) return;

    const auto *lambdaAssignment = program.statements[0].expressionRoots.empty()
        ? nullptr
        : findExpression(program, program.statements[0].expressionRoots.front());
    const auto *lambda = lambdaAssignment == nullptr
        ? nullptr
        : findExpression(program, lambdaAssignment->right);
    expect(lambda != nullptr && lambda->kind == vietvm::frontend::AstExpressionKind::Lambda &&
               lambda->parameters == std::vector<std::string>({"x", "y"}) &&
               lambda->bodyTokenBegin < lambda->bodyTokenEnd,
           "lambda AST stores parameters and a lossless body token range");

    const auto *mapAssignment = program.statements[1].expressionRoots.empty()
        ? nullptr
        : findExpression(program, program.statements[1].expressionRoots.front());
    const auto *map = mapAssignment == nullptr
        ? nullptr
        : findExpression(program, mapAssignment->right);
    expect(map != nullptr && map->kind == vietvm::frontend::AstExpressionKind::MapLiteral &&
               map->mapEntries.size() == 5,
           "map literal AST owns a key/value edge for every entry");
    if (map == nullptr || map->mapEntries.size() != 5) return;

    const auto *stringKey = findExpression(program, map->mapEntries[0].key);
    const auto *integerValue = findExpression(program, map->mapEntries[0].value);
    const auto *booleanValue = findExpression(program, map->mapEntries[1].value);
    const auto *nullValue = findExpression(program, map->mapEntries[2].value);
    const auto *floatValue = findExpression(program, map->mapEntries[3].value);
    const auto *stringValue = findExpression(program, map->mapEntries[4].value);
    expect(stringKey != nullptr && stringKey->literalKind == vietvm::frontend::AstLiteralKind::String &&
               integerValue != nullptr && integerValue->literalKind == vietvm::frontend::AstLiteralKind::Integer &&
               booleanValue != nullptr && booleanValue->literalKind == vietvm::frontend::AstLiteralKind::Boolean &&
               nullValue != nullptr && nullValue->literalKind == vietvm::frontend::AstLiteralKind::Null &&
               floatValue != nullptr && floatValue->literalKind == vietvm::frontend::AstLiteralKind::Float &&
               stringValue != nullptr && stringValue->literalKind == vietvm::frontend::AstLiteralKind::String,
           "literal nodes preserve integer, float, string, boolean and null categories");
}

void testExpressionRootsOnDeclarationsAndControlFlow() {
    const vietvm::frontend::AstProgram program = parseSource(
        "hàm riêng tư tính tổng(a, b = 2) { trả về a + b; }\n"
        "nếu (a + b * 2 > 4) { in a; }\n"
        "lặp (i = 0; i < 3; i++) { ném i; }\n"
        "chọn (a + b) {}");
    const auto *function = findFunction(program, "tính tổng");
    expect(function != nullptr &&
               function->visibility == vietvm::frontend::AstVisibility::Private &&
               function->parameters.size() == 2 &&
               function->parameters[0].name == "a" && function->parameters[1].name == "b",
           "function AST exposes visibility and named parameter payloads");
    if (function != nullptr && function->parameters.size() == 2) {
        expect(!function->parameters[0].hasDefault && function->parameters[1].hasDefault,
               "function parameters distinguish absent defaults from present defaults");
        const auto *defaultValue = findExpression(program, function->parameters[1].defaultValue);
        expect(defaultValue != nullptr &&
                   defaultValue->literalKind == vietvm::frontend::AstLiteralKind::Integer &&
                   defaultValue->text == "2",
               "default parameter value points into the expression arena");
        expect(!function->children.empty() && !function->children.front().children.empty() &&
                   function->children.front().children.front().expressionRoots.size() == 1,
               "return statements inside declaration bodies receive expression roots");
    }

    const auto findTopLevel = [&](vietvm::frontend::AstStatementKind kind)
        -> const vietvm::frontend::AstStatement * {
        for (const auto &statement : program.statements) {
            if (statement.kind == kind) return &statement;
        }
        return nullptr;
    };
    const auto *conditional = findTopLevel(vietvm::frontend::AstStatementKind::Conditional);
    const auto *loop = findTopLevel(vietvm::frontend::AstStatementKind::Loop);
    const auto *selection = findTopLevel(vietvm::frontend::AstStatementKind::Switch);
    expect(conditional != nullptr && conditional->expressionRoots.size() == 1,
           "conditional header exposes its condition root");
    expect(loop != nullptr && loop->expressionRoots.size() == 3,
           "loop header exposes init, condition and update roots in source order");
    expect(selection != nullptr && selection->expressionRoots.size() == 1,
           "switch header exposes its selector root");
}

void testExpressionAstFallsBackWithoutLosingStatementTokens() {
    const vietvm::frontend::AstProgram program = parseSource("in danh_sách[0];");
    expect(program.statements.size() == 1 &&
               program.statements.front().kind == vietvm::frontend::AstStatementKind::Print &&
               program.statements.front().expressionRoots.empty(),
           "unsupported expression syntax keeps the statement and declines to attach a partial root");
    expect(program.tokens.size() == 6 && program.tokens[1].lexeme == "danh_sách" &&
               program.tokens[2].lexeme == "[" && program.tokens[4].lexeme == "]",
           "tolerant expression fallback retains the complete legacy token range");
}

void testExpressionRollbackPreservesArenaIdsAndGroupedSpans() {
    const vietvm::frontend::AstProgram program = parseSource(
        "in a[0];\n"
        "in b + 1;\n"
        "hàm f(a = 1, unsupported = q[0], c = 2) { trả về (c + 1); }");
    expect(program.statements.size() == 3 &&
               program.statements[0].expressionRoots.empty() &&
               program.statements[1].expressionRoots.size() == 1,
           "a failed expression slice rolls back without suppressing the next statement root");

    const auto *function = findFunction(program, "f");
    expect(function != nullptr && function->parameters.size() == 3,
           "failed default parsing keeps the complete function parameter payload");
    if (function != nullptr && function->parameters.size() == 3) {
        expect(function->parameters[0].hasDefault &&
                   function->parameters[0].defaultValue != vietvm::frontend::kInvalidExprId &&
                   function->parameters[1].hasDefault &&
                   function->parameters[1].defaultValue == vietvm::frontend::kInvalidExprId &&
                   function->parameters[2].hasDefault &&
                   function->parameters[2].defaultValue != vietvm::frontend::kInvalidExprId,
               "default presence survives fallback while surrounding default roots remain valid");
    }

    const auto validId = [&](vietvm::frontend::ExprId id) {
        return id == vietvm::frontend::kInvalidExprId || id < program.expressions.size();
    };
    for (std::size_t index = 0; index < program.expressions.size(); ++index) {
        const auto &expression = program.expressions[index];
        expect(expression.id == index,
               "rollback leaves expression IDs contiguous and equal to arena indices");
        expect(expression.tokenBegin < expression.tokenEnd &&
                   expression.tokenEnd <= program.tokens.size() &&
                   expression.span.begin.offset ==
                       program.tokens[expression.tokenBegin].span.begin.offset &&
                   expression.span.end.offset ==
                       program.tokens[expression.tokenEnd - 1].span.end.offset,
               "grouped and ordinary expressions keep spans aligned to their token ranges");
        expect(validId(expression.operand) && validId(expression.left) &&
                   validId(expression.right) && validId(expression.callee),
               "rollback leaves no dangling fixed expression edge");
        for (vietvm::frontend::ExprId argument : expression.arguments) {
            expect(validId(argument), "rollback leaves no dangling call argument edge");
        }
        for (const auto &entry : expression.mapEntries) {
            expect(validId(entry.key) && validId(entry.value),
                   "rollback leaves no dangling map entry edge");
        }
    }
}

void testSemanticForwardAndDynamicCalls() {
    const std::string source =
        "gọi sau();\n"
        "gọi native_chưa_biết();\n"
        "hàm sau() { trả về 1; }";
    const vietvm::frontend::AstProgram program = parseSource(source);
    const vietvm::compiler::SemanticModel model = vietvm::compiler::analyzeSemantics(program);

    expect(!model.hasErrors(), "forward and native calls are semantically valid");
    const vietvm::frontend::AstStatement *declaration = findFunction(program, "sau");
    const vietvm::compiler::SemanticReference *forward = findReference(model, "sau");
    const vietvm::compiler::SemanticReference *native = findReference(model, "native_chưa_biết");

    expect(declaration != nullptr, "forward-call test has a parsed function declaration");
    expect(forward != nullptr, "semantic analysis records the forward direct call");
    if (declaration != nullptr && forward != nullptr) {
        expect(!forward->dynamic && forward->resolvedSymbolId >= 0 &&
                   forward->resolvedSymbolId == model.symbolForDeclaration(declaration->tokenBegin),
               "semantic analysis predeclares and resolves a function called before its declaration");
    }

    expect(native != nullptr, "semantic analysis records the unknown native-style call");
    if (native != nullptr) {
        expect(native->dynamic && native->resolvedSymbolId == -1,
               "unknown native-style calls remain dynamic instead of becoming errors");
    }
}

void testSemanticBuildsScopesAndDeclarations() {
    const vietvm::frontend::AstProgram program = parseSource(
        "nhập src/tests/modules/math.vi như toan;\n"
        "lớp công khai Toan {\n"
        "  hàm riêng tư cộng(a, b) {\n"
        "    nếu (a) { tạm = b; }\n"
        "    thử { in a; } bắt lỗi (e) { in e; }\n"
        "  }\n"
        "}\n"
        "hàm main(p) {\n"
        "  local = p;\n"
        "  đếm++;\n"
        "  handler = hàm(x) { trả về x; };\n"
        "}");
    const vietvm::compiler::SemanticModel model =
        vietvm::compiler::analyzeSemantics(program);

    expect(model.globalScope == 0 && !model.scopes.empty() &&
               model.scopes.front().kind == vietvm::compiler::ScopeKind::Global &&
               model.scopes.front().parent == vietvm::compiler::kInvalidScopeId,
           "semantic scope zero is the parentless global scope");
    for (std::size_t index = 0; index < model.scopes.size(); ++index) {
        expect(model.scopes[index].id == index,
               "ScopeId is the deterministic index in the semantic scope arena");
    }
    for (std::size_t index = 0; index < model.symbols.size(); ++index) {
        expect(model.symbols[index].id == index,
               "SymbolId is the deterministic index in declaration order");
    }

    const auto countScopes = [&](vietvm::compiler::ScopeKind kind) {
        return std::count_if(model.scopes.begin(), model.scopes.end(),
                             [&](const auto &scope) { return scope.kind == kind; });
    };
    expect(countScopes(vietvm::compiler::ScopeKind::Class) == 1 &&
               countScopes(vietvm::compiler::ScopeKind::Function) == 2 &&
               countScopes(vietvm::compiler::ScopeKind::Lambda) == 1 &&
               countScopes(vietvm::compiler::ScopeKind::Catch) == 1 &&
               countScopes(vietvm::compiler::ScopeKind::Block) >= 5,
           "scope tree represents class, functions, blocks, lambda and catch independently");
    for (const auto &scope : model.scopes) {
        for (vietvm::compiler::ScopeId child : scope.children) {
            expect(child < model.scopes.size() && model.scopes[child].parent == scope.id,
                   "every scope child has a reciprocal deterministic parent link");
        }
    }

    const auto *classSymbol = findSymbol(
        model, vietvm::compiler::SemanticSymbolKind::Class, "Toan");
    const auto *method = findSymbol(
        model, vietvm::compiler::SemanticSymbolKind::Method, "cộng");
    const auto *alias = findSymbol(
        model, vietvm::compiler::SemanticSymbolKind::ImportAlias, "toan");
    const auto *parameterA = findSymbol(
        model, vietvm::compiler::SemanticSymbolKind::Parameter, "a");
    const auto *local = findSymbol(
        model, vietvm::compiler::SemanticSymbolKind::LocalVariable, "tạm");
    const auto *postfixLocal = findSymbol(
        model, vietvm::compiler::SemanticSymbolKind::LocalVariable, "đếm");
    const auto *catchVariable = findSymbol(
        model, vietvm::compiler::SemanticSymbolKind::CatchVariable, "e");
    expect(classSymbol != nullptr && classSymbol->memberScope != vietvm::compiler::kInvalidScopeId,
           "class symbol owns its class member scope");
    expect(method != nullptr && method->qualifiedName == "Toan.cộng" &&
               classSymbol != nullptr && method->ownerClass == classSymbol->id &&
               method->visibility == vietvm::compiler::SemanticVisibility::Private,
           "method declaration retains class ownership, qualification and visibility");
    expect(alias != nullptr && alias->space == vietvm::compiler::SymbolSpace::Module,
           "import alias is declared in the module namespace");
    expect(parameterA != nullptr &&
               model.scopes[parameterA->declaringScope].kind ==
                   vietvm::compiler::ScopeKind::Function,
           "function parameter is declared in its function scope");
    expect(local != nullptr &&
               model.scopes[local->declaringScope].kind ==
                   vietvm::compiler::ScopeKind::Function,
           "implicit assignment inside a nested block is function-scoped for legacy compatibility");
    expect(postfixLocal != nullptr &&
               model.scopes[postfixLocal->declaringScope].kind ==
                   vietvm::compiler::ScopeKind::Function,
           "first-use postfix update declares the same implicit local as the legacy compiler");
    expect(catchVariable != nullptr &&
               model.scopes[catchVariable->declaringScope].kind ==
                   vietvm::compiler::ScopeKind::Catch,
           "catch variable belongs only to the catch scope");
}

void testSemanticResolvesExpressionNamesAndCallKinds() {
    const vietvm::frontend::AstProgram program = parseSource(
        "hàm sau(value) { trả về value; }\n"
        "hàm main(callback) {\n"
        "  local = 1;\n"
        "  in sau(local);\n"
        "  in callback(local);\n"
        "  gọi native_exact(local);\n"
        "  gọi missing(local);\n"
        "  in unknown_value;\n"
        "}");
    vietvm::compiler::SemanticEnvironment environment;
    environment.nativeCallables.push_back("native_exact");
    const vietvm::compiler::SemanticModel model =
        vietvm::compiler::analyzeSemantics(
            program, environment, vietvm::compiler::ResolutionPolicy::PreserveLegacy);

    const auto expectCallKind = [&](const std::string &name,
                                    vietvm::compiler::CallTargetKind expected,
                                    const std::string &message) {
        const auto *call = findCallByCallee(program, name);
        const auto *binding = call == nullptr
            ? nullptr
            : model.callBindingForExpression(call->id);
        expect(binding != nullptr && binding->kind == expected, message);
    };
    expectCallKind("sau", vietvm::compiler::CallTargetKind::DirectFunction,
                   "declared function call resolves to a direct semantic target");
    expectCallKind("callback", vietvm::compiler::CallTargetKind::IndirectValue,
                   "parameter call resolves to an indirect value target");
    expectCallKind("native_exact", vietvm::compiler::CallTargetKind::Native,
                   "only an explicitly supplied native name resolves as native");
    expectCallKind("missing", vietvm::compiler::CallTargetKind::DynamicName,
                   "unknown call keeps the dynamic-name compatibility fallback");

    const auto *directCall = findCallByCallee(program, "sau");
    const auto *directCallee = directCall == nullptr
        ? nullptr
        : program.expression(directCall->callee);
    const auto *directBinding = directCallee == nullptr
        ? nullptr
        : model.bindingForExpression(directCallee->id);
    const auto *function = findSymbol(
        model, vietvm::compiler::SemanticSymbolKind::Function, "sau");
    expect(directBinding != nullptr &&
               directBinding->kind == vietvm::compiler::BindingKind::Symbol &&
               function != nullptr && directBinding->symbol == function->id,
           "callee ExprId binds to the deterministic semantic SymbolId");

    const auto unknownName = std::find_if(
        program.expressions.begin(), program.expressions.end(),
        [](const auto &expression) {
            return expression.kind == vietvm::frontend::AstExpressionKind::Name &&
                   expression.text == "unknown_value";
        });
    const auto *unknownBinding = unknownName == program.expressions.end()
        ? nullptr
        : model.bindingForExpression(unknownName->id);
    expect(unknownBinding != nullptr &&
               unknownBinding->kind == vietvm::compiler::BindingKind::LegacyImplicitValue,
           "unknown value read is explicit in the model instead of silently becoming a symbol");

    const auto *local = findSymbol(
        model, vietvm::compiler::SemanticSymbolKind::LocalVariable, "local");
    expect(local != nullptr && model.scopeForExpression(unknownName == program.expressions.end()
                                                            ? vietvm::frontend::kInvalidExprId
                                                            : unknownName->id) !=
                                   vietvm::compiler::kInvalidScopeId,
           "expression ownership records a lexical scope for later recursive lowering");
}

void testSemanticKeepsSiblingFunctionBindingsIsolated() {
    const vietvm::frontend::AstProgram program = parseSource(
        "hàm owner(secret) { local = secret; }\n"
        "hàm consumer() { in local; gọi secret(); }");
    const vietvm::compiler::SemanticModel model =
        vietvm::compiler::analyzeSemantics(program);

    const vietvm::frontend::AstExpression *consumerLocal = nullptr;
    for (const auto &expression : program.expressions) {
        if (expression.kind == vietvm::frontend::AstExpressionKind::Name &&
            expression.text == "local" && expression.span.begin.line == 2) {
            consumerLocal = &expression;
            break;
        }
    }
    const auto *localBinding = consumerLocal == nullptr
        ? nullptr
        : model.bindingForExpression(consumerLocal->id);
    expect(localBinding != nullptr &&
               localBinding->kind == vietvm::compiler::BindingKind::LegacyImplicitValue &&
               localBinding->symbol == vietvm::compiler::kInvalidSymbolId,
           "a local from one function cannot resolve as a value in a sibling function");

    const auto *secretCall = findCallByCallee(program, "secret");
    const auto *secretBinding = secretCall == nullptr
        ? nullptr
        : model.callBindingForExpression(secretCall->id);
    expect(secretBinding != nullptr &&
               secretBinding->kind == vietvm::compiler::CallTargetKind::DynamicName &&
               secretBinding->symbol == vietvm::compiler::kInvalidSymbolId,
           "a parameter from one function cannot resolve as a callable in a sibling function");
}

void testSemanticAssignmentReusesCatchBindingAndPostfixDeclaresOnce() {
    const vietvm::frontend::AstProgram program = parseSource(
        "hàm main() {\n"
        "  đếm++;\n"
        "  thử { in 1; } bắt lỗi (e) { e = \"x\"; in e; }\n"
        "}");
    const vietvm::compiler::SemanticModel model =
        vietvm::compiler::analyzeSemantics(program);

    const auto countSymbols = [&](vietvm::compiler::SemanticSymbolKind kind,
                                  const std::string &name) {
        return std::count_if(model.symbols.begin(), model.symbols.end(),
                             [&](const auto &symbol) {
                                 return symbol.kind == kind && symbol.lookupName == name;
                             });
    };
    expect(countSymbols(vietvm::compiler::SemanticSymbolKind::LocalVariable, "đếm") == 1,
           "a first-use postfix update creates exactly one function-scoped local");
    expect(countSymbols(vietvm::compiler::SemanticSymbolKind::CatchVariable, "e") == 1 &&
               countSymbols(vietvm::compiler::SemanticSymbolKind::LocalVariable, "e") == 0,
           "assignment in a catch body reuses the visible catch binding without a local shadow");
}

void testSemanticResolvesClassMembersAndEnforcesVisibility() {
    const vietvm::frontend::AstProgram allowedProgram = parseSource(
        "lớp Toan {\n"
        "  hàm riêng tư nội bộ(x) { trả về x; }\n"
        "  hàm công khai gọi nội bộ(x) { trả về nội bộ(x); }\n"
        "}\n"
        "hàm main() { in Toan.gọi nội bộ(1); }");
    const vietvm::compiler::SemanticModel allowed =
        vietvm::compiler::analyzeSemantics(allowedProgram);
    expect(!allowed.hasErrors(),
           "same-class private call and externally qualified public call are valid");
    const auto *internalCall = findCallByCallee(allowedProgram, "nội bộ");
    const auto *qualifiedCall = findCallByCallee(allowedProgram, "Toan.gọi nội bộ");
    expect(internalCall != nullptr &&
               allowed.callBindingForExpression(internalCall->id) != nullptr &&
               allowed.callBindingForExpression(internalCall->id)->kind ==
                   vietvm::compiler::CallTargetKind::DirectFunction,
           "unqualified call resolves through the enclosing class scope");
    expect(qualifiedCall != nullptr &&
               allowed.callBindingForExpression(qualifiedCall->id) != nullptr &&
               allowed.callBindingForExpression(qualifiedCall->id)->kind ==
                   vietvm::compiler::CallTargetKind::DirectFunction,
           "qualified class method name resolves to the same direct symbol model");

    const vietvm::frontend::AstProgram deniedProgram = parseSource(
        "lớp Toan { hàm riêng tư bí mật() { trả về 1; } }\n"
        "hàm main() { in Toan.bí mật(); }");
    const vietvm::compiler::SemanticModel denied =
        vietvm::compiler::analyzeSemantics(deniedProgram);
    const bool hasPrivateDiagnostic = std::any_of(
        denied.diagnostics.begin(), denied.diagnostics.end(),
        [](const auto &diagnostic) { return diagnostic.code == "VPP-SEM-001"; });
    expect(denied.hasErrors() && hasPrivateDiagnostic,
           "private method access outside its class emits the stable semantic diagnostic");

    const vietvm::frontend::AstProgram inheritedProgram = parseSource(
        "lớp riêng tư Kho { hàm ẩn() { trả về 1; } }\n"
        "hàm main() { in Kho.ẩn(); tham_chiếu = Kho.ẩn; }");
    const vietvm::compiler::SemanticModel inherited =
        vietvm::compiler::analyzeSemantics(inheritedProgram);
    const auto *inheritedMethod = findSymbol(
        inherited, vietvm::compiler::SemanticSymbolKind::Method, "ẩn");
    const auto privateDiagnosticCount = std::count_if(
        inherited.diagnostics.begin(), inherited.diagnostics.end(),
        [](const auto &diagnostic) { return diagnostic.code == "VPP-SEM-001"; });
    expect(inheritedMethod != nullptr &&
               inheritedMethod->visibility == vietvm::compiler::SemanticVisibility::Private,
           "an unannotated method inherits its declaring class visibility");
    expect(privateDiagnosticCount == 2,
           "private-member validation covers both a method call and a method used as a value");
}

void testSemanticFallbackCoversOnlyMissingLoopHeaderParts() {
    const vietvm::frontend::AstProgram program = parseSource(
        "hàm known() { trả về 1; }\n"
        "lặp(i = 0; known(items[0]); i++) { thoát; }");
    expect(program.statements.size() == 2 &&
               program.statements[1].expressionRoots.size() == 2,
           "loop regression fixture has two AST roots and one token-backed header part");
    const vietvm::compiler::SemanticModel model =
        vietvm::compiler::analyzeSemantics(program);
    const auto *known = findSymbol(
        model, vietvm::compiler::SemanticSymbolKind::Function, "known");
    const auto *reference = findReference(model, "known");
    expect(known != nullptr && reference != nullptr && !reference->dynamic &&
               reference->resolvedSymbolId == static_cast<int>(known->id),
           "semantic fallback resolves calls in a loop part that lacks an expression root");
    const auto referenceCount = std::count_if(
        model.references.begin(), model.references.end(),
        [](const auto &candidate) { return candidate.name == "known"; });
    expect(referenceCount == 1,
           "loop fallback does not duplicate references covered by existing expression roots");
}

void testSemanticStrictPolicyRejectsUnresolvedNames() {
    const vietvm::frontend::AstProgram program = parseSource(
        "in missing_value; gọi missing_call();");
    const vietvm::compiler::SemanticModel model =
        vietvm::compiler::analyzeSemantics(
            program, {}, vietvm::compiler::ResolutionPolicy::Strict);

    const auto hasCode = [&](const std::string &code) {
        return std::any_of(model.diagnostics.begin(), model.diagnostics.end(),
                           [&](const auto &diagnostic) {
                               return diagnostic.code == code;
                           });
    };
    expect(model.hasErrors() && hasCode("VPP-SEM-006") && hasCode("VPP-SEM-007"),
           "strict resolution reports unresolved values and calls with stable diagnostics");
}

void testSemanticDuplicateDeclarationDiagnostic() {
    const vietvm::frontend::AstProgram program = parseSource(
        "hàm trùng() { }\n"
        "hàm trùng() { }\n"
        "lớp TrùngLớp { }\n"
        "lớp TrùngLớp { }");
    const vietvm::compiler::SemanticModel model = vietvm::compiler::analyzeSemantics(program);

    const bool hasDuplicateDiagnostic = std::any_of(
        model.diagnostics.begin(), model.diagnostics.end(),
        [](const vietvm::compiler::SemanticDiagnostic &diagnostic) {
            return diagnostic.severity == vietvm::compiler::SemanticDiagnosticSeverity::Error &&
                   diagnostic.code == "VPP-SEM-004";
        });
    expect(model.hasErrors(), "duplicate declarations are semantic errors");
    expect(hasDuplicateDiagnostic,
           "duplicate declarations receive the stable semantic diagnostic code");

    const auto expectSingleOwnedScope = [&](vietvm::compiler::SemanticSymbolKind symbolKind,
                                            vietvm::compiler::ScopeKind scopeKind,
                                            const std::string &name,
                                            const std::string &message) {
        const auto *symbol = findSymbol(model, symbolKind, name);
        const auto ownedScopeCount = symbol == nullptr
            ? 0
            : std::count_if(model.scopes.begin(), model.scopes.end(),
                            [&](const auto &scope) {
                                return scope.kind == scopeKind &&
                                       scope.ownerSymbol == symbol->id;
                            });
        expect(symbol != nullptr && ownedScopeCount == 1 &&
                   symbol->memberScope < model.scopes.size() &&
                   model.scopes[symbol->memberScope].ownerSymbol == symbol->id,
               message);
    };
    expectSingleOwnedScope(vietvm::compiler::SemanticSymbolKind::Function,
                           vietvm::compiler::ScopeKind::Function, "trùng",
                           "a duplicate function cannot replace or add ownership for the first symbol");
    expectSingleOwnedScope(vietvm::compiler::SemanticSymbolKind::Class,
                           vietvm::compiler::ScopeKind::Class, "TrùngLớp",
                           "a duplicate class cannot replace or add ownership for the first symbol");
}

void testIrLoweringPreservesTokensAndOptimizerRemovesNoOps() {
    const vietvm::frontend::AstProgram program = parseSource("; in 1;");
    const vietvm::compiler::SemanticModel semantic = vietvm::compiler::analyzeSemantics(program);
    vietvm::compiler::IrProgram ir = vietvm::compiler::lowerToIr(program, semantic);

    const std::vector<std::string> sourceTokens = vietvm::frontend::tokenLexemes(program.tokens);
    expect(vietvm::compiler::materializeIrTokens(ir) == sourceTokens,
           "IR lowering preserves the AST token sequence losslessly");
    expect(ir.instructions.size() == 2 &&
               ir.instructions.front().opcode == vietvm::compiler::IrOpcode::NoOp,
           "an empty statement lowers to an explicit IR no-op");

    const vietvm::compiler::OptimizationReport report = vietvm::compiler::optimizeIr(ir);
    expect(report.removedNoOps == 1,
           "IR optimizer reports removal of the empty-statement no-op");
    expect(ir.instructions.size() == 1 &&
               ir.instructions.front().opcode == vietvm::compiler::IrOpcode::Print,
           "IR optimizer leaves the real statement after removing the no-op");
    expect(vietvm::compiler::materializeIrTokens(ir) ==
               std::vector<std::string>({"in", "1", ";"}),
           "IR optimization removes only the no-op token sequence");
}

void testCompilePipelineProducesBasicBytecodeShape() {
    vietvm::compiler::resetCompilationState();
    try {
        const vietvm::compiler::CompilationArtifacts artifacts =
            vietvm::compiler::compilePipeline(
                "hàm main() { trả về 7; }", keywordMap, true);

        expect(!artifacts.bytecode.empty(), "compilePipeline returns bytecode");
        expect(!artifacts.semantic.hasErrors(),
               "compilePipeline retains the successful semantic model in its artifacts");
        expect(!artifacts.ir.instructions.empty(),
               "compilePipeline exposes the lowered IR artifacts");
        if (!artifacts.bytecode.empty()) {
            expect(artifacts.bytecode.back().op == OP_DUNG_CHUONG_TRINH,
                   "pipeline bytecode ends with the program-stop instruction");
        }
        const bool callsMain = std::any_of(
            artifacts.bytecode.begin(), artifacts.bytecode.end(),
            [](const Instruction &instruction) { return instruction.op == OP_GOI; });
        expect(callsMain,
               "pipeline bytecode includes the top-level call that starts main");
    } catch (const std::exception &error) {
        expect(false, std::string("compilePipeline unexpectedly threw: ") + error.what());
    }
    vietvm::compiler::resetCompilationState();
}

} // namespace

int main() {
    testSpanCarryingTokensAndMultiwordKeyword();
    testParserBuildsFunctionAstWithNestedReturnAndRange();
    testParserReportsStructuralErrorsWithSpans();
    testParserReportsUnmatchedClosingBracket();
    testParserReportsUnclosedOpeningBracket();
    testParserRejectsCrossedDelimiterNesting();
    testExpressionAstUsesStableIdsAndLegacyPrecedence();
    testExpressionAstCallsAssignmentsAndPostfix();
    testExpressionAstLambdaMapAndLiteralCategories();
    testExpressionRootsOnDeclarationsAndControlFlow();
    testExpressionAstFallsBackWithoutLosingStatementTokens();
    testExpressionRollbackPreservesArenaIdsAndGroupedSpans();
    testSemanticForwardAndDynamicCalls();
    testSemanticBuildsScopesAndDeclarations();
    testSemanticResolvesExpressionNamesAndCallKinds();
    testSemanticKeepsSiblingFunctionBindingsIsolated();
    testSemanticAssignmentReusesCatchBindingAndPostfixDeclaresOnce();
    testSemanticResolvesClassMembersAndEnforcesVisibility();
    testSemanticFallbackCoversOnlyMissingLoopHeaderParts();
    testSemanticStrictPolicyRejectsUnresolvedNames();
    testSemanticDuplicateDeclarationDiagnostic();
    testIrLoweringPreservesTokensAndOptimizerRemovesNoOps();
    testCompilePipelineProducesBasicBytecodeShape();

    if (failures != 0) {
        std::cerr << failures << " pipeline unit test(s) failed\n";
        return 1;
    }

    std::cout << "pipeline unit tests passed\n";
    return 0;
}
