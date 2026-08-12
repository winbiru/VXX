#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "common/storeString.h"
#include "frontend/keywords.h"
#include "vpp/compiler/compiler.h"
#include "vpp/compiler/pipeline.h"

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

bool sameInstruction(const Instruction &left, const Instruction &right) {
    return left.op == right.op && left.operand == right.operand &&
           left.operandIndex == right.operandIndex &&
           left.operandValue == right.operandValue;
}

void expectBytecode(const std::vector<Instruction> &actual,
                    const std::vector<Instruction> &expected,
                    const std::string &message) {
    if (actual.size() != expected.size()) {
        expect(false, message + " (instruction count)");
        return;
    }
    for (std::size_t index = 0; index < actual.size(); ++index) {
        if (!sameInstruction(actual[index], expected[index])) {
            expect(false, message + " (instruction " + std::to_string(index) + ")");
            return;
        }
    }
}

std::string compileError(const std::string &source) {
    vietvm::compiler::resetCompilationState();
    try {
        (void)vietvm::compiler::compilePipeline(source, keywordMap, false);
    } catch (const std::exception &error) {
        const std::string message = error.what();
        vietvm::compiler::resetCompilationState();
        return message;
    }
    vietvm::compiler::resetCompilationState();
    return {};
}

void expectSameDiagnosticAsForcedBridge(const std::string &source,
                                        const std::string &diagnosticCode,
                                        const std::string &message) {
    const std::string selectedPathError = compileError(source);
    const std::string forcedBridgeError = compileError("trả về; " + source);
    expect(!selectedPathError.empty() &&
               selectedPathError.find(diagnosticCode) != std::string::npos,
           message + " (pipeline diagnostic)");
    expect(selectedPathError == forcedBridgeError,
           message + " (same diagnostic as forced legacy bridge)");
}

void testDirectLiteralAndBinaryPrint() {
    vietvm::compiler::resetCompilationState();
    const auto artifacts = vietvm::compiler::compilePipeline(
        "in 1 + 2 * 3;", keywordMap, true);

    expect(artifacts.backend == vietvm::compiler::BytecodeBackend::DirectIr,
           "safe arithmetic print uses the direct IR backend");
    expect(artifacts.legacyFallbackRegions == 0,
           "safe arithmetic print reports zero codegen fallbacks");
    expectBytecode(
        artifacts.bytecode,
        {{OP_BIEN_SO, 1, 0, 0},
         {OP_BIEN_SO, 2, 0, 0},
         {OP_BIEN_SO, 3, 0, 0},
         {OP_NHAN, 0, 0, 0},
         {OP_CONG, 0, 0, 0},
         {OP_IN, 0, 0, 0},
         {OP_DUNG_CHUONG_TRINH, 0, 0, 0}},
        "direct emitter follows recursive expression evaluation order");
    vietvm::compiler::resetCompilationState();
}

void testDirectAssignmentsOwnVmSlots() {
    vietvm::compiler::resetCompilationState();
    const auto artifacts = vietvm::compiler::compilePipeline(
        "x = 2; x += 3; in x;", keywordMap, true);

    expect(artifacts.backend == vietvm::compiler::BytecodeBackend::DirectIr,
           "simple name assignment cohort uses direct IR emission");
    expectBytecode(
        artifacts.bytecode,
        {{OP_BIEN_SO, 2, 0, 0},
         {OP_TEN_BIEN_ID, 0, 0, 0},
         {OP_GAN, 0, 0, 0},
         {OP_TEN_BIEN_GIA_TRI, 0, 0, 0},
         {OP_BIEN_SO, 3, 0, 0},
         {OP_CONG, 0, 0, 0},
         {OP_TEN_BIEN_ID, 0, 0, 0},
         {OP_GAN, 0, 0, 0},
         {OP_TEN_BIEN_GIA_TRI, 0, 0, 0},
         {OP_IN, 0, 0, 0},
         {OP_DUNG_CHUONG_TRINH, 0, 0, 0}},
        "direct emitter maps one runtime name to one VM slot");
    vietvm::compiler::resetCompilationState();
}

void testUnsupportedCallUsesExplicitBridge() {
    vietvm::compiler::resetCompilationState();
    const auto artifacts = vietvm::compiler::compilePipeline(
        "in native_chưa_biết(1);", keywordMap, true);

    expect(artifacts.backend == vietvm::compiler::BytecodeBackend::LegacyTokenBridge,
           "dynamic calls remain on the compatibility backend");
    expect(artifacts.legacyFallbackRegions > 0,
           "compatibility selection exposes a nonzero fallback count");
    expect(!artifacts.bytecode.empty() &&
               artifacts.bytecode.back().op == OP_DUNG_CHUONG_TRINH,
           "fallback compilation still produces executable bytecode");
    vietvm::compiler::resetCompilationState();
}

void testPrimitiveMapUsesDirectIrAndLegacyEncoding() {
    vietvm::compiler::resetCompilationState();
    const auto artifacts = vietvm::compiler::compilePipeline(
        "m = {\"a\": 1, \"b\": \"x\", \"c\": rỗng}; in m;", keywordMap, true);

    expect(artifacts.backend == vietvm::compiler::BytecodeBackend::DirectIr,
           "primitive map literal uses structured direct IR emission");
    const auto &pool = vietvm::compiler::StringPool::getPool();
    const std::string expectedEncoding =
        std::string("a\x1f" "i\x1f" "1\x1e" "b\x1f" "s\x1f" "x\x1e" "c\x1f" "n\x1f");
    expect(pool.size() == 1 && pool.front() == expectedEncoding,
           "direct map emitter preserves the VM RS/FS StringPool contract");
    expect(!artifacts.bytecode.empty() && artifacts.bytecode.front().op == OP_MAP_LITERAL &&
               artifacts.bytecode.front().operandIndex == 0,
           "direct map emitter produces OP_MAP_LITERAL with the encoded pool index");
    vietvm::compiler::resetCompilationState();
}

void testMapEscapesMatchForcedLegacyEncoding() {
    const std::string source =
        R"VPP(m = {"line\nkey": "x\ny", "quote": "a\"b", "slash": "c\\d"}; in m;)VPP";

    vietvm::compiler::resetCompilationState();
    const auto direct = vietvm::compiler::compilePipeline(
        source, keywordMap, false);
    const std::vector<std::string> directPool =
        vietvm::compiler::StringPool::getPool();
    expect(direct.backend == vietvm::compiler::BytecodeBackend::DirectIr,
           "escaped primitive map uses direct IR");

    vietvm::compiler::resetCompilationState();
    const auto forcedLegacy = vietvm::compiler::compilePipeline(
        "trả về; " + source, keywordMap, false);
    const std::vector<std::string> legacyPool =
        vietvm::compiler::StringPool::getPool();
    expect(forcedLegacy.backend ==
               vietvm::compiler::BytecodeBackend::LegacyTokenBridge,
           "an unsupported return statement forces the map through the bridge");

    const std::string expectedEncoding =
        std::string("line\\nkey\x1f" "s\x1f" "x\\ny\x1e") +
        "quote\x1f" "s\x1f" "a\"b\x1e" +
        "slash\x1f" "s\x1f" "c\\\\d";
    expect(directPool == legacyPool,
           "direct and forced-legacy map encoders produce the same StringPool bytes");
    expect(directPool.size() == 1 && directPool.front() == expectedEncoding,
           "map strings decode source escapes before applying RS/FS escaping");
    vietvm::compiler::resetCompilationState();
}

void testMapGrammarAndContextFallBackToLegacyDiagnostics() {
    expectSameDiagnosticAsForcedBridge(
        "m = {hello world: 1};",
        "VPP-SYN-026",
        "a contextual multi-word name is not accepted as one legacy map key");
    expectSameDiagnosticAsForcedBridge(
        "in !{\"a\": 1};",
        "VPP-SYN-033",
        "a map nested under a unary operator remains a legacy diagnostic");
    expectSameDiagnosticAsForcedBridge(
        "m = {\"a\": 1} + 2;",
        "VPP-SYN-033",
        "a map nested under a binary assignment RHS remains a legacy diagnostic");
    expectSameDiagnosticAsForcedBridge(
        "in ({\"a\": 1});",
        "VPP-SYN-033",
        "a parenthesized map remains outside the legacy whole-map grammar");
    expectSameDiagnosticAsForcedBridge(
        "m = {(\"a\"): 1};",
        "VPP-SYN-026",
        "a parenthesized key remains outside the legacy map-key grammar");
    expectSameDiagnosticAsForcedBridge(
        "m = {\"a\": (1)};",
        "VPP-SYN-029",
        "a parenthesized value remains outside the legacy primitive-value grammar");
}

void testGroupedStoresFallBackToLegacyDiagnostics() {
    for (const std::string &source : {
             std::string("(x) = 1;"),
             std::string("(x)++;"),
             std::string("(x = 1);"),
             std::string("(x += 1);"),
             std::string("x = 0; (x++);"),
         }) {
        expectSameDiagnosticAsForcedBridge(
            source,
            "VPP-SYN-032",
            "a grouped store keeps the legacy mismatched-parentheses diagnostic");
    }
}

} // namespace

int main() {
    testDirectLiteralAndBinaryPrint();
    testDirectAssignmentsOwnVmSlots();
    testUnsupportedCallUsesExplicitBridge();
    testPrimitiveMapUsesDirectIrAndLegacyEncoding();
    testMapEscapesMatchForcedLegacyEncoding();
    testMapGrammarAndContextFallBackToLegacyDiagnostics();
    testGroupedStoresFallBackToLegacyDiagnostics();

    if (failures != 0) {
        std::cerr << failures << " direct codegen unit test(s) failed\n";
        return 1;
    }
    std::cout << "direct codegen unit tests passed\n";
    return 0;
}
