#include <exception>
#include <iostream>
#include <string>

#include "frontend/keywords.h"
#include "vpp/compiler/compiler.h"
#include "vpp/tooling/tooling.h"

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
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

        expect(dump.rfind("IR instructions=2\n", 0) == 0,
               "IR dump reports the post-optimization instruction count");
        expect(dump.find("print ") != std::string::npos,
               "IR dump renders the print opcode name");
        expect(dump.find("define_function ") != std::string::npos,
               "IR dump renders the function-definition opcode name");
        expect(dump.find("noop") == std::string::npos,
               "IR dump reflects that the optimizer removed no-op instructions");
        expect(dump.find("tokens=[\"in\", \"1\", \";\"]") != std::string::npos,
               "IR dump quotes token lexemes without ambiguity");
        expect(dump.find("symbol=") != std::string::npos,
               "IR dump includes semantic symbol IDs");
        expect(dump.find("span=2:1..") != std::string::npos,
               "IR dump exposes source line and column spans");
    } catch (const std::exception &error) {
        expect(false, std::string("IR dump compilation unexpectedly threw: ") + error.what());
    }
    vietvm::compiler::resetCompilationState();
}

} // namespace

int main() {
    testAstDumpUsesStructuralNamesAndTree();
    testIrDumpUsesOptimizedPipelineInstructions();

    if (failures != 0) {
        std::cerr << failures << " tooling unit test(s) failed\n";
        return 1;
    }
    std::cout << "tooling unit tests passed\n";
    return 0;
}
