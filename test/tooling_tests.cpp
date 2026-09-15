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

        expect(dump.rfind("AST từ tố=", 0) == 0,
               "AST dump starts with its program summary");
        expect(dump.find("\nhàm ") != std::string::npos,
               "AST dump renders the function statement kind");
        expect(dump.find("\n  khối ") != std::string::npos,
               "AST dump indents the function body block");
        expect(dump.find("\n    trả về ") != std::string::npos,
               "AST dump renders nested return statements");
        expect(dump.find("khai báo=\"chào\"") != std::string::npos,
               "AST dump quotes a declaration name with UTF-8 text intact");
        expect(dump.find("từ tố=[0, ") != std::string::npos,
               "AST dump exposes each node's half-open token range");
        expect(dump.find("vị trí=1:1..") != std::string::npos,
               "AST dump exposes source line and column spans");
        expect(dump.find("biểu thức:\n") != std::string::npos &&
                   dump.find(" kiểu trực tiếp=") != std::string::npos,
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

        expect(dump.rfind("IR câu lệnh=2 giá trị=", 0) == 0,
               "IR dump reports the post-optimization instruction count");
        expect(dump.find("vùng chưa hỗ trợ trực tiếp=") != std::string::npos &&
                   dump.find("giá trị:\n") != std::string::npos &&
                   dump.find("câu lệnh:\n") != std::string::npos,
               "IR dump exposes structured values and explicit unsupported-direct accounting");
        expect(dump.find("in ") != std::string::npos,
               "IR dump renders the print opcode name");
        expect(dump.find("định nghĩa hàm ") != std::string::npos,
               "IR dump renders the function-definition opcode name");
        expect(dump.find("khai báo=\"chào\"") != std::string::npos &&
                   dump.find("tham số=[") != std::string::npos,
               "IR dump exposes declaration and structured parameter metadata");
        expect(dump.find("không thao tác") == std::string::npos,
               "IR dump reflects that the optimizer removed no-op instructions");
        expect(dump.find("từ tố=[\"in\", \"1\", \";\"]") != std::string::npos,
               "IR dump quotes token lexemes without ambiguity");
        expect(dump.find("hằng số nguyên") != std::string::npos &&
                   dump.find("gốc=[") != std::string::npos,
               "IR dump makes recursive expression roots inspectable");
        expect(dump.find("ký hiệu=") != std::string::npos,
               "IR dump includes semantic symbol IDs");
        expect(dump.find("vị trí=2:1..") != std::string::npos,
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
        expect(ast.find("điều kiện ") != std::string::npos &&
                   ast.find("dạng=khối nếu hoặc") != std::string::npos,
               "AST dump exposes parser-owned conditional form metadata");
        expect(ir.find("điều kiện ") != std::string::npos &&
                   ir.find("dạng=khối nếu hoặc") != std::string::npos,
               "IR dump exposes lowered conditional form metadata");
    } catch (const std::exception &error) {
        expect(false, std::string("conditional dump unexpectedly threw: ") + error.what());
    }
    vietvm::compiler::resetCompilationState();
}

void testStructuredImportPayloadIsVisibleInAstDump() {
    const vietvm::frontend::AstProgram program = parseSource(
        "nhập src/tests/gói/math.vi như toan;\n"
        "nhập \"cốt lõi\";");
    const std::string dump = vietvm::tooling::dumpAst(program);

    expect(dump.find("nhập ") != std::string::npos &&
               dump.find("dạng=tệp nguồn cục bộ ") != std::string::npos &&
               dump.find("đích=\"src/tests/gói/math.vi\"") !=
                   std::string::npos &&
               dump.find("có dấu nháy=không dấu chấm phẩy=có bí danh=\"toan\"") !=
                   std::string::npos,
           "AST dump exposes structured local-file import spelling and alias metadata");
    expect(dump.find("đích=\"cốt lõi\" có dấu nháy=có dấu chấm phẩy=có") !=
               std::string::npos,
           "AST dump exposes structured quoted package-import metadata");
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
        expect(ast.find("lặp ") != std::string::npos &&
                   ast.find("dạng=khối lặp") != std::string::npos,
               "AST dump exposes parser-owned loop form metadata");
        expect(ir.find("lặp ") != std::string::npos &&
                   ir.find("dạng=khối lặp") != std::string::npos,
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
        expect(ast.find("lớp ") != std::string::npos &&
                   ast.find("khai báo=\"Toan\" phạm vi=riêng tư "
                            "dạng=khối phương thức") != std::string::npos,
               "AST dump exposes exact class form and source visibility");
        expect(ast.find("khai báo=\"cộng\" phạm vi=công khai") !=
                   std::string::npos,
               "AST dump keeps a method's unqualified source declaration");
        expect(ir.find("định nghĩa lớp ") != std::string::npos &&
                   ir.find("khai báo=\"Toan\" phạm vi=riêng tư "
                           "dạng=khối phương thức") != std::string::npos,
               "IR dump exposes lowered class form and visibility");
        expect(ir.find("định nghĩa hàm ") != std::string::npos &&
                   ir.find("khai báo=\"Toan.cộng\" phạm vi=công khai") !=
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
        const std::size_t firstAstBlock = ast.find("\n  khối ");
        const std::size_t firstIrBlock = ir.find("  khối ");
        expect(ast.find("thử ") != std::string::npos &&
                   ast.find("dạng=khối thử bắt lỗi biến bắt lỗi=\"e\"") !=
                       std::string::npos &&
                   firstAstBlock != std::string::npos &&
                   ast.find("\n  khối ", firstAstBlock + 1) != std::string::npos,
               "AST dump exposes try form, catch binding and both block children");
        expect(ir.find("thử ") != std::string::npos &&
                   ir.find("dạng=khối thử bắt lỗi biến bắt lỗi=\"e\":ký hiệu=") !=
                       std::string::npos &&
                   firstIrBlock != std::string::npos &&
                   ir.find("  khối ", firstIrBlock + 1) != std::string::npos,
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

    expect(ast.find(" hàm vô danh=1 ") != std::string::npos &&
               ast.find(" hàm vô danh=#0") != std::string::npos &&
               ast.find("tham số=[\"x\", \"y\":mặc định=#") !=
                   std::string::npos &&
               ast.find("\n    khối ") != std::string::npos &&
               ast.find("\n      trả về ") != std::string::npos,
           "AST dump exposes lambda arena identity, parameters/default and recursive body");
    expect(ir.find(" hàm vô danh=1 vùng chưa hỗ trợ trực tiếp=0") != std::string::npos &&
               ir.find(" hàm vô danh=#0") != std::string::npos &&
               ir.find("#0 chủ sở hữu=#") != std::string::npos &&
               ir.find("tham số=[\"x\":ký hiệu=") != std::string::npos &&
               ir.find(":mặc định=#") != std::string::npos &&
               ir.find("biến bắt giữ=[") != std::string::npos &&
               ir.find("\n    0  khối ") != std::string::npos,
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
    expect(dump.find("đích gọi=hàm trực tiếp") != std::string::npos &&
               dump.find("đích gọi=giá trị gián tiếp") != std::string::npos &&
               dump.find("đích gọi=hàm bản địa") != std::string::npos &&
               dump.find("đích gọi=tên động") != std::string::npos,
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

    expect(dump.find("OP_BIEN_SO toán hạng=7 chỉ số=0 giá trị=0 bể chuỗi=") == std::string::npos,
           "integer constants are not mislabeled as StringPool references");
    expect(dump.find("OP_PARAM toán hạng=0 chỉ số=0 giá trị=0 bể chuỗi=") == std::string::npos,
           "parameter local slots are not mislabeled as StringPool references");
    expect(dump.find("OP_CHUOI toán hạng=0 chỉ số=0 giá trị=0 bể chuỗi=\"text\"") != std::string::npos,
           "string literals label their operandIndex pool entry");
    expect(dump.find("OP_HAM toán hạng=1 chỉ số=3 giá trị=0 bể chuỗi=\"function\"") != std::string::npos,
           "function declarations label their operand name entry");
    expect(dump.find("OP_GOI toán hạng=0 chỉ số=-2 giá trị=0 bể chuỗi=\"function\"") != std::string::npos,
           "name-based calls decode their negative operandIndex pool entry");
    expect(dump.find("OP_PARAM_MAC_DINH toán hạng=2 chỉ số=0 giá trị=0 bể chuỗi=\"i:2\"") !=
               std::string::npos,
           "default parameters label the pool entry stored in operand");
}

void testFormatterPreservesCommentsTokensAndIsIdempotent() {
    const std::string source =
        "// giữ bình luận đầu\n"
        "hàm main(){x=1+2;/* giữ bình luận giữa */nếu(x>1){in \"Việt Nam\";}"
        "lặp(i=0;i<2;i++){in i;}}\n";

    const std::string formatted = vietvm::tooling::formatSource(source);
    const std::string formattedAgain = vietvm::tooling::formatSource(formatted);

    expect(formatted.find("// giữ bình luận đầu") != std::string::npos &&
               formatted.find("/* giữ bình luận giữa */") != std::string::npos,
           "formatter preserves line and block comments");
    expect(formatted.find("nếu (x > 1)") != std::string::npos,
           "formatter uses stable control-flow parenthesis spacing");
    expect(formatted.find("lặp (i = 0; i < 2; i++)") != std::string::npos,
           "formatter keeps for-loop header semicolons on one logical line");
    expect(formatted.find("\"Việt Nam\"") != std::string::npos,
           "formatter preserves UTF-8 string literal bytes");
    expect(formatted == formattedAgain,
           "formatter is idempotent");

    const auto before = vietvm::compiler::postProcessTokens(
        vietvm::compiler::tokenize(source));
    const auto after = vietvm::compiler::postProcessTokens(
        vietvm::compiler::tokenize(formatted));
    expect(before == after,
           "formatter preserves the compiler token stream");
}

void testLintDiagnosticsExposeSourceLocations() {
    const auto valid = vietvm::tooling::lintDiagnostics(
        "hàm main() { in \"đúng\"; }");
    expect(valid.empty(), "linter accepts valid source without diagnostics");

    const auto lexerError = vietvm::tooling::lintDiagnostics(
        "hàm main() {\n  in \"chuỗi chưa đóng\n}");
    expect(!lexerError.empty() &&
               lexerError.front().severity == vietvm::tooling::DiagnosticSeverity::Error &&
               lexerError.front().span.begin.line == 2 &&
               lexerError.front().span.begin.column > 0,
           "lexer diagnostic carries the actual source line and column");

    const auto parseError = vietvm::tooling::lintDiagnostics(
        "in 1;\n}\n");
    expect(!parseError.empty() &&
               parseError.front().severity == vietvm::tooling::DiagnosticSeverity::Error &&
               parseError.front().span.begin.line == 2 &&
               parseError.front().span.begin.column == 1,
           "parser diagnostic carries its structural source span");

    std::string compatibilityError;
    expect(!vietvm::tooling::lintSource("in 1;\n}\n", compatibilityError) &&
               !compatibilityError.empty(),
           "legacy lintSource API remains compatible with structured diagnostics");
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
    testFormatterPreservesCommentsTokensAndIsIdempotent();
    testLintDiagnosticsExposeSourceLocations();

    if (failures != 0) {
        std::cerr << failures << " tooling unit test(s) failed\n";
        return 1;
    }
    std::cout << "tooling unit tests passed\n";
    return 0;
}
