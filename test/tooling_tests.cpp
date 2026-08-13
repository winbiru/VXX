#include <exception>
#include <iostream>
#include <string>

#include "frontend/keywords.h"
#include "frontend/lexer.h"
#include "vpp/compiler/compiler.h"
#include "vpp/compiler/semantic.h"
#include "vpp/frontend/parser.h"
#include "vpp/tooling/tooling.h"

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

vietvm::frontend::AstProgram parseSource(const std::string &source) {
    return vietvm::frontend::parseTokens(
        vietvm::compiler::postProcessTokensWithSpans(
            vietvm::compiler::tokenizeWithSpans(source)));
}

void testAstDumpUsesStructuralNamesAndTree() {
    vietvm::compiler::resetCompilationState();
    try {
        const vietvm::compiler::CompilationArtifacts artifacts =
            vietvm::compiler::compilePipeline(
                "hàm chào() {\n  trả về 1;\n}", keywordMap, false);
        const std::string dump = vietvm::tooling::dumpAst(artifacts.ast);

        expect(dump.rfind("AST tokens=", 0) == 0,
               "AST dump starts with its program summary");
        expect(dump.find("\nfunction ") != std::string::npos,
               "AST dump renders the function statement kind");
        expect(dump.find("\n  block ") != std::string::npos,
               "AST dump indents the function body block");
        expect(dump.find("\n    return ") != std::string::npos,
               "AST dump renders nested return statements");
        expect(dump.find("declaration=\"chào\"") != std::string::npos,
               "AST dump quotes a declaration name with UTF-8 text intact");
        expect(dump.find("tokens=[0, ") != std::string::npos,
               "AST dump exposes each node's half-open token range");
        expect(dump.find("span=1:1..") != std::string::npos,
               "AST dump exposes source line and column spans");
        expect(dump.find("expressions:\n") != std::string::npos &&
                   dump.find(" literal=") != std::string::npos,
               "AST dump exposes expression kinds and literal categories");
    } catch (const std::exception &error) {
        expect(false, std::string("AST dump compilation unexpectedly threw: ") + error.what());
    }
    vietvm::compiler::resetCompilationState();
}

void testIrDumpUsesOptimizedPipelineInstructions() {
    vietvm::compiler::resetCompilationState();
    try {
        const vietvm::compiler::CompilationArtifacts artifacts =
            vietvm::compiler::compilePipeline(
                ";\n"
                "in 1;\n"
                "hàm chào() { trả về 1; }",
                keywordMap, false);
        const std::string dump = vietvm::tooling::dumpIr(artifacts.ir);

        expect(dump.rfind("IR instructions=2 values=", 0) == 0,
               "IR dump reports the post-optimization instruction count");
        expect(dump.find("legacy-regions=") != std::string::npos &&
                   dump.find("values:\n") != std::string::npos &&
                   dump.find("instructions:\n") != std::string::npos,
               "IR dump exposes structured values and explicit fallback accounting");
        expect(dump.find("print ") != std::string::npos,
               "IR dump renders the print opcode name");
        expect(dump.find("define_function ") != std::string::npos,
               "IR dump renders the function-definition opcode name");
        expect(dump.find("declaration=\"chào\"") != std::string::npos &&
                   dump.find("params=[") != std::string::npos,
               "IR dump exposes declaration and structured parameter metadata");
        expect(dump.find("noop") == std::string::npos,
               "IR dump reflects that the optimizer removed no-op instructions");
        expect(dump.find("tokens=[\"in\", \"1\", \";\"]") != std::string::npos,
               "IR dump quotes token lexemes without ambiguity");
        expect(dump.find("const_int") != std::string::npos &&
                   dump.find("roots=[") != std::string::npos,
               "IR dump makes recursive expression roots inspectable");
        expect(dump.find("symbol=") != std::string::npos,
               "IR dump includes semantic symbol IDs");
        expect(dump.find("span=2:1..") != std::string::npos,
               "IR dump exposes source line and column spans");
    } catch (const std::exception &error) {
        expect(false, std::string("IR dump compilation unexpectedly threw: ") + error.what());
    }
    vietvm::compiler::resetCompilationState();
}

void testConditionalFormIsVisibleInAstAndIrDumps() {
    vietvm::compiler::resetCompilationState();
    try {
        const vietvm::compiler::CompilationArtifacts artifacts =
            vietvm::compiler::compilePipeline(
                "nếu (đúng) { in 1; } hoặc { in 2; }",
                keywordMap, false);
        const std::string ast = vietvm::tooling::dumpAst(artifacts.ast);
        const std::string ir = vietvm::tooling::dumpIr(artifacts.ir);
        expect(ast.find("conditional ") != std::string::npos &&
                   ast.find("form=if_else_blocks") != std::string::npos,
               "AST dump exposes parser-owned conditional form metadata");
        expect(ir.find("conditional ") != std::string::npos &&
                   ir.find("form=if_else_blocks") != std::string::npos,
               "IR dump exposes lowered conditional form metadata");
    } catch (const std::exception &error) {
        expect(false, std::string("conditional dump unexpectedly threw: ") + error.what());
    }
    vietvm::compiler::resetCompilationState();
}

void testStructuredImportPayloadIsVisibleInAstDump() {
    const vietvm::frontend::AstProgram program = parseSource(
        "nhập src/tests/thư viện/math.vi như toan;\n"
        "nhập \"cốt lõi\";");
    const std::string dump = vietvm::tooling::dumpAst(program);

    expect(dump.find("import ") != std::string::npos &&
               dump.find("form=local_source_file ") != std::string::npos &&
               dump.find("target=\"src/tests/thư viện/math.vi\"") !=
                   std::string::npos &&
               dump.find("quoted=no semicolon=yes alias=\"toan\"") !=
                   std::string::npos,
           "AST dump exposes structured local-file import spelling and alias metadata");
    expect(dump.find("form=unstructured") != std::string::npos,
           "AST dump identifies package imports that remain on the tolerant path");
}

void testLoopFormIsVisibleInAstAndIrDumps() {
    vietvm::compiler::resetCompilationState();
    try {
        const vietvm::compiler::CompilationArtifacts artifacts =
            vietvm::compiler::compilePipeline(
                "lặp (i = 0; i < 1; i++) { in i; }",
                keywordMap, false);
        const std::string ast = vietvm::tooling::dumpAst(artifacts.ast);
        const std::string ir = vietvm::tooling::dumpIr(artifacts.ir);
        expect(ast.find("loop ") != std::string::npos &&
                   ast.find("form=for_block") != std::string::npos,
               "AST dump exposes parser-owned loop form metadata");
        expect(ir.find("loop ") != std::string::npos &&
                   ir.find("form=for_block") != std::string::npos,
               "IR dump exposes lowered loop form metadata");
    } catch (const std::exception &error) {
        expect(false, std::string("loop dump unexpectedly threw: ") + error.what());
    }
    vietvm::compiler::resetCompilationState();
}

void testClassFormVisibilityAndQualifiedMethodsAreVisibleInDumps() {
    vietvm::compiler::resetCompilationState();
    try {
        const vietvm::compiler::CompilationArtifacts artifacts =
            vietvm::compiler::compilePipeline(
                "lớp riêng tư Toan { "
                "hàm công khai cộng() { trả về 1; } }",
                keywordMap, false);
        const std::string ast = vietvm::tooling::dumpAst(artifacts.ast);
        const std::string ir = vietvm::tooling::dumpIr(artifacts.ir);
        expect(ast.find("class ") != std::string::npos &&
                   ast.find("declaration=\"Toan\" visibility=private "
                            "form=method_block") != std::string::npos,
               "AST dump exposes exact class form and source visibility");
        expect(ast.find("declaration=\"cộng\" visibility=public") !=
                   std::string::npos,
               "AST dump keeps a method's unqualified source declaration");
        expect(ir.find("define_class ") != std::string::npos &&
                   ir.find("declaration=\"Toan\" visibility=private "
                           "form=method_block") != std::string::npos,
               "IR dump exposes lowered class form and visibility");
        expect(ir.find("define_function ") != std::string::npos &&
                   ir.find("declaration=\"Toan.cộng\" visibility=public") !=
                       std::string::npos,
               "IR dump exposes the semantic qualified method name");
    } catch (const std::exception &error) {
        expect(false, std::string("class dump unexpectedly threw: ") + error.what());
    }
    vietvm::compiler::resetCompilationState();
}

void testTryFormAndCatchBindingAreVisibleInAstAndIrDumps() {
    vietvm::compiler::resetCompilationState();
    try {
        const vietvm::compiler::CompilationArtifacts artifacts =
            vietvm::compiler::compilePipeline(
                "thử { ném \"boom\"; } bắt lỗi (e) { in e; }",
                keywordMap, false);
        const std::string ast = vietvm::tooling::dumpAst(artifacts.ast);
        const std::string ir = vietvm::tooling::dumpIr(artifacts.ir);
        const std::size_t firstAstBlock = ast.find("\n  block ");
        const std::size_t firstIrBlock = ir.find("  block ");
        expect(ast.find("try ") != std::string::npos &&
                   ast.find("form=try_catch_blocks catch=\"e\"") !=
                       std::string::npos &&
                   firstAstBlock != std::string::npos &&
                   ast.find("\n  block ", firstAstBlock + 1) != std::string::npos,
               "AST dump exposes try form, catch binding and both block children");
        expect(ir.find("try ") != std::string::npos &&
                   ir.find("form=try_catch_blocks catch=\"e\":symbol=") !=
                       std::string::npos &&
                   firstIrBlock != std::string::npos &&
                   ir.find("  block ", firstIrBlock + 1) != std::string::npos,
               "IR dump exposes lowered try form, catch SymbolId and both blocks");
    } catch (const std::exception &error) {
        expect(false, std::string("try/catch dump unexpectedly threw: ") + error.what());
    }
    vietvm::compiler::resetCompilationState();
}

void testStructuredLambdaPayloadsAreVisibleInAstAndIrDumps() {
    using namespace vietvm::compiler;
    const vietvm::frontend::AstProgram program = parseSource(
        "hàm outer(p) { "
        "handler = hàm(x, y = 2) { trả về x + y + p; }; }");
    const SemanticModel semantic = analyzeSemantics(program);
    const IrProgram irProgram = lowerToIr(program, semantic);
    const std::string ast = vietvm::tooling::dumpAst(program);
    const std::string ir = vietvm::tooling::dumpIr(irProgram);

    expect(ast.find(" lambdas=1 ") != std::string::npos &&
               ast.find(" lambda=#0") != std::string::npos &&
               ast.find("params=[\"x\", \"y\":default=#") !=
                   std::string::npos &&
               ast.find("\n    block ") != std::string::npos &&
               ast.find("\n      return ") != std::string::npos,
           "AST dump exposes lambda arena identity, parameters/default and recursive body");
    expect(ir.find(" lambdas=1 legacy-regions=0") != std::string::npos &&
               ir.find(" lambda=#0") != std::string::npos &&
               ir.find("#0 owner=#") != std::string::npos &&
               ir.find("params=[\"x\":symbol=") != std::string::npos &&
               ir.find(":default=#") != std::string::npos &&
               ir.find("captures=[") != std::string::npos &&
               ir.find("\n    0  block ") != std::string::npos,
           "IR dump exposes lambda owner, semantic parameters/captures and recursive block");
}

void testCallTargetKindsAreVisibleInIrDump() {
    using namespace vietvm::compiler;
    IrProgram program;
    const auto addCall = [&](IrValueOpcode opcode, CallTargetKind target) {
        IrValue value;
        value.id = program.values.size();
        value.opcode = opcode;
        value.callTarget = target;
        program.values.push_back(std::move(value));
    };
    addCall(IrValueOpcode::Call, CallTargetKind::DirectFunction);
    addCall(IrValueOpcode::CallDynamic, CallTargetKind::IndirectValue);
    addCall(IrValueOpcode::CallDynamic, CallTargetKind::Native);
    addCall(IrValueOpcode::CallDynamic, CallTargetKind::DynamicName);

    const std::string dump = vietvm::tooling::dumpIr(program);
    expect(dump.find("call-target=direct_function") != std::string::npos &&
               dump.find("call-target=indirect_value") != std::string::npos &&
               dump.find("call-target=native") != std::string::npos &&
               dump.find("call-target=dynamic_name") != std::string::npos,
           "IR dump renders semantic direct/indirect/native/dynamic call classification");
}

void testDisassemblerLabelsOnlyRealStringPoolOperands() {
    const std::vector<Instruction> bytecode = {
        {OP_BIEN_SO, 7, 0, 0},
        {OP_CHUOI, 0, 0, 0},
        {OP_HAM, 1, 3, 0},
        {OP_GOI, 0, -2, 0},
        {OP_PARAM, 0, 0, 0},
        {OP_PARAM_MAC_DINH, 2, 0, 0},
    };
    const std::string dump = vietvm::tooling::disassembleBytecode(
        bytecode, {"text", "function", "i:2"});

    expect(dump.find("OP_BIEN_SO op=7 idx=0 val=0 pool=") == std::string::npos,
           "integer constants are not mislabeled as StringPool references");
    expect(dump.find("OP_PARAM op=0 idx=0 val=0 pool=") == std::string::npos,
           "parameter local slots are not mislabeled as StringPool references");
    expect(dump.find("OP_CHUOI op=0 idx=0 val=0 pool=\"text\"") != std::string::npos,
           "string literals label their operandIndex pool entry");
    expect(dump.find("OP_HAM op=1 idx=3 val=0 pool=\"function\"") != std::string::npos,
           "function declarations label their operand name entry");
    expect(dump.find("OP_GOI op=0 idx=-2 val=0 pool=\"function\"") != std::string::npos,
           "name-based calls decode their negative operandIndex pool entry");
    expect(dump.find("OP_PARAM_MAC_DINH op=2 idx=0 val=0 pool=\"i:2\"") !=
               std::string::npos,
           "default parameters label the pool entry stored in operand");
}

} // namespace

int main() {
    testAstDumpUsesStructuralNamesAndTree();
    testIrDumpUsesOptimizedPipelineInstructions();
    testStructuredImportPayloadIsVisibleInAstDump();
    testConditionalFormIsVisibleInAstAndIrDumps();
    testLoopFormIsVisibleInAstAndIrDumps();
    testClassFormVisibilityAndQualifiedMethodsAreVisibleInDumps();
    testTryFormAndCatchBindingAreVisibleInAstAndIrDumps();
    testStructuredLambdaPayloadsAreVisibleInAstAndIrDumps();
    testCallTargetKindsAreVisibleInIrDump();
    testDisassemblerLabelsOnlyRealStringPoolOperands();

    if (failures != 0) {
        std::cerr << failures << " tooling unit test(s) failed\n";
        return 1;
    }
    std::cout << "tooling unit tests passed\n";
    return 0;
}
