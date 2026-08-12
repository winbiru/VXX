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

void testSemanticDuplicateDeclarationDiagnostic() {
    const vietvm::frontend::AstProgram program = parseSource(
        "hàm trùng() { }\n"
        "hàm trùng() { }");
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
    testSemanticForwardAndDynamicCalls();
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
