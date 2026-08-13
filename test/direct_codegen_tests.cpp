#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <unordered_map>
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

bool sameBytecode(const std::vector<Instruction> &left,
                  const std::vector<Instruction> &right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!sameInstruction(left[index], right[index])) return false;
    }
    return true;
}

struct CompilerState {
    vietvm::compiler::BytecodeBackend backend =
        vietvm::compiler::BytecodeBackend::LegacyTokenBridge;
    std::vector<Instruction> root;
    std::vector<std::string> pool;
    std::unordered_map<int, std::vector<Instruction>> functions;
    std::unordered_map<int, int> functionNames;
};

CompilerState compileState(const std::string &source,
                           bool emitMainCall = false) {
    vietvm::compiler::resetCompilationState();
    const auto artifacts = vietvm::compiler::compilePipeline(
        source, keywordMap, emitMainCall);
    return {artifacts.backend,
            artifacts.bytecode,
            vietvm::compiler::StringPool::getPool(),
            vietvm::compiler::hamMap::hamBytecodeMap,
            vietvm::compiler::hamMap::hamNameIndexMap};
}

void expectDirectMatchesForcedBridge(const std::string &source,
                                     const std::string &message,
                                     bool emitMainCall = false) {
    const CompilerState direct = compileState(source, emitMainCall);
    CompilerState bridge = compileState("trả về;\n" + source, emitMainCall);
    expect(direct.backend == vietvm::compiler::BytecodeBackend::DirectIr,
           message + " (selected direct backend)");
    expect(bridge.backend == vietvm::compiler::BytecodeBackend::LegacyTokenBridge,
           message + " (forced bridge backend)");
    if (bridge.root.size() >= 2 && bridge.root[0].op == OP_BIEN_SO &&
        bridge.root[1].op == OP_TRA_VE) {
        bridge.root.erase(bridge.root.begin(), bridge.root.begin() + 2);
        for (Instruction &instruction : bridge.root) {
            if ((instruction.op == OP_JUMP ||
                 instruction.op == OP_JUMP_IF_FALSE ||
                 instruction.op == OP_THU ||
                 instruction.op == OP_THU_KET_THUC) &&
                instruction.operand >= 2) {
                instruction.operand -= 2;
            }
        }
    } else {
        expect(false, message + " (forced suffix shape)");
    }
    const bool rootMatches = sameBytecode(direct.root, bridge.root);
    if (!rootMatches) {
        std::cerr << "DETAIL: " << message << " direct-root=" << direct.root.size()
                  << " bridge-root=" << bridge.root.size() << '\n';
        const std::size_t common = std::min(direct.root.size(), bridge.root.size());
        for (std::size_t index = 0; index < common; ++index) {
            if (sameInstruction(direct.root[index], bridge.root[index])) continue;
            const Instruction &left = direct.root[index];
            const Instruction &right = bridge.root[index];
            std::cerr << "DETAIL: first root difference @" << index
                      << " direct={" << static_cast<int>(left.op) << ','
                      << left.operand << ',' << left.operandIndex << ','
                      << left.operandValue << "} bridge={"
                      << static_cast<int>(right.op) << ',' << right.operand << ','
                      << right.operandIndex << ',' << right.operandValue << "}\n";
            break;
        }
    }
    expect(rootMatches, message + " (root bytecode)");
    expect(direct.pool == bridge.pool, message + " (StringPool)");
    expect(direct.functionNames == bridge.functionNames,
           message + " (function-name registry)");
    expect(direct.functions.size() == bridge.functions.size(),
           message + " (function registry size)");
    for (const auto &entry : direct.functions) {
        const auto found = bridge.functions.find(entry.first);
        expect(found != bridge.functions.end() &&
                   sameBytecode(entry.second, found->second),
               message + " (function " + std::to_string(entry.first) + " bytecode)");
    }
    vietvm::compiler::resetCompilationState();
}

void expectUsesLegacyBridge(const std::string &source,
                            const std::string &message) {
    vietvm::compiler::resetCompilationState();
    const auto artifacts = vietvm::compiler::compilePipeline(
        source, keywordMap, false);
    expect(artifacts.backend ==
               vietvm::compiler::BytecodeBackend::LegacyTokenBridge &&
               artifacts.legacyFallbackRegions > 0,
           message);
    vietvm::compiler::resetCompilationState();
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

void expectSameErrorAsForcedBridge(const std::string &source,
                                   const std::string &message) {
    const std::string selectedPathError = compileError(source);
    const std::string forcedBridgeError = compileError("trả về; " + source);
    expect(!selectedPathError.empty(), message + " (pipeline diagnostic)");
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

void testDirectDynamicAndIndirectCallsMatchLegacyState() {
    expectDirectMatchesForcedBridge(
        "in native_chưa_biết(1);",
        "an unresolved/native name call uses the legacy StringPool fallback",
        true);
    expectDirectMatchesForcedBridge(
        "gọi missing(1);",
        "an unresolved explicit call retains its name-based fallback");
    expectDirectMatchesForcedBridge(
        "hàm inc(x) { trả về x + 1; } callback = inc; in callback(2);",
        "a variable holding a function id emits an indirect call");
    expectDirectMatchesForcedBridge(
        "callback = 1; in callback(2);",
        "an existing textual slot takes the legacy indirect-call path");
    expectDirectMatchesForcedBridge(
        "in callback(2); callback = 1;",
        "a call before its textual slot exists retains name-fallback timing");
    expectDirectMatchesForcedBridge(
        "in missing(\"arg\") + 1;",
        "nested dynamic call emits arguments before storing its callable name");
    expectDirectMatchesForcedBridge(
        "missing()",
        "a semicolon-free standalone dynamic call keeps dedicated-call behavior");
    expectDirectMatchesForcedBridge(
        "missing(\"missing\");",
        "a callable name deduplicates against an earlier argument string");
    expectDirectMatchesForcedBridge(
        "outer(\"a\", inner(\"b\"));",
        "nested dynamic calls preserve argument and callable-name pool order");
    expectDirectMatchesForcedBridge(
        "hàm apply(f, x) { trả về f(x); } "
        "hàm inc(x) { trả về x + 1; } "
        "hàm main() { in apply(inc, 2); }",
        "a callback parameter lowers to the legacy indirect-call sequence");
    expectDirectMatchesForcedBridge(
        "hàm seed(callback) { trả về 0; } "
        "hàm use() { trả về callback(1); }",
        "shared textual slots from an earlier function retain legacy call dispatch");
    expectDirectMatchesForcedBridge(
        "callback = 1; gọi callback();",
        "explicit gọi syntax keeps name fallback even when a variable slot exists");

    const CompilerState nested = compileState("in missing(\"arg\") + 1;");
    expect(nested.pool.size() == 2 && nested.pool[0] == "arg" &&
               nested.pool[1] == "missing",
           "dynamic call StringPool order follows argument evaluation");
    vietvm::compiler::resetCompilationState();

    const CompilerState nestedNames =
        compileState("outer(\"a\", inner(\"b\"));");
    expect(nestedNames.pool ==
               std::vector<std::string>({"a", "b", "inner", "outer"}),
           "nested dynamic call names enter StringPool after their arguments");
    vietvm::compiler::resetCompilationState();
}

void testDirectFunctionsParametersReturnsAndCalls() {
    const std::string source =
        "hàm cộng(a, b = 2) { trả về a + b; }\n"
        "hàm main() { in cộng(3); }";
    expectDirectMatchesForcedBridge(
        source,
        "structured functions, primitive defaults, returns and resolved calls match legacy");

    const CompilerState state = compileState(source);
    expect(state.pool == std::vector<std::string>({"cộng", "main", "i:2"}),
           "function names are predeclared before default values enter StringPool");
    expect(state.root.size() == 2 && state.root[0].op == OP_HAM &&
               state.root[0].operand == 0 && state.root[0].operandIndex == 0 &&
               state.root[1].op == OP_HAM && state.root[1].operand == 1 &&
               state.root[1].operandIndex == 1,
           "direct function declarations preserve legacy name-index and function-id operands");
    const auto add = state.functions.find(0);
    expect(add != state.functions.end() && add->second.size() == 10 &&
               add->second[1].op == OP_KHOI_TAO &&
               add->second[2].op == OP_PARAM &&
               add->second[3].op == OP_KHOI_TAO &&
               add->second[4].op == OP_PARAM_MAC_DINH &&
               add->second[8].op == OP_TRA_VE,
           "direct function body emits parameter binding and return opcodes from IR");
    vietvm::compiler::resetCompilationState();
}

void testDirectMultiwordFunctionsMatchLegacy() {
    const std::string source =
        "hàm main() {\n"
        "  in cộng hai số(2, 3);\n"
        "  gọi in lời chào();\n"
        "  in cộng ba số(1, 2, 3);\n"
        "}\n"
        "hàm cộng hai số(a, b) { trả về a + b; }\n"
        "hàm in lời chào() { in \"xin chào\"; }\n"
        "hàm cộng ba số(a, b, c) { trả về cộng hai số(a, b) + c; }";
    expectDirectMatchesForcedBridge(
        source,
        "multiword forward, explicit and nested direct calls match legacy state");
}

void testExplicitCallOnlyUsesItsStatementGrammar() {
    expectUsesLegacyBridge(
        "hàm f() { trả về 7; } hàm main() { in gọi f(); }",
        "an explicit call nested under print stays on the legacy grammar");
    expectUsesLegacyBridge(
        "hàm f() { trả về 7; } hàm main() { in (gọi f()); }",
        "grouping does not erase the explicit-call grammar marker");
    expectUsesLegacyBridge(
        "hàm f() { trả về 7; } hàm main() { trả về gọi f(); }",
        "an explicit call nested under return stays on the bridge");
    expectUsesLegacyBridge(
        "hàm f() { trả về 7; } hàm g(x) { trả về x; } "
        "hàm main() { g(gọi f()); }",
        "an explicit call used as another call argument stays on the bridge");

    expectDirectMatchesForcedBridge(
        "hàm f() { trả về 7; } hàm main() { gọi f() }",
        "a standalone explicit call retains its semicolon-optional legacy form");
}

void testCallStatementMustConsumeTheWholeExpression() {
    expectUsesLegacyBridge(
        "hàm f(x) { trả về x; } hàm main() { f(1) + 2; in 9; }",
        "a leading call with trailing expression text stays on legacy dispatch");
    expectUsesLegacyBridge(
        "hàm f() { trả về 1; } hàm main() { (f()) + 2; }",
        "a grouped call used inside a plain statement expression stays on legacy parsing");
    expectUsesLegacyBridge(
        "hàm f() { trả về 1; } hàm main() { !f(); }",
        "a non-root call in a plain statement expression stays on legacy parsing");
}

void testStatementLeadingKeywordCallsStayOnBridge() {
    expectSameDiagnosticAsForcedBridge(
        "hàm gọi() { trả về 7; } hàm main() { gọi(); in 1; }",
        "VPP-SYN-017",
        "a normal call named 'gọi' remains owned by the statement keyword handler");
    expectUsesLegacyBridge(
        "hàm dừng x() { trả về 7; } hàm main() { dừng x(); in 1; }",
        "a call beginning with an opcode keyword remains on legacy statement dispatch");
}

void testCallStatementArgumentSplittingMatchesLegacy() {
    expectSameDiagnosticAsForcedBridge(
        "hàm f(a, b) { trả về a + b; } "
        "hàm main() { f(\"x,y\", \"z\"); in 1; }",
        "VPP-LEX-001",
        "a comma inside a normal call-statement string keeps legacy argument splitting");
    expectSameDiagnosticAsForcedBridge(
        "hàm f(a, b) { trả về a + b; } "
        "hàm main() { gọi f(\"x,y\", \"z\"); in 1; }",
        "VPP-LEX-001",
        "a comma inside an explicit-call string keeps legacy argument splitting");
    expectSameDiagnosticAsForcedBridge(
        "hàm f(a, b) { trả về a + b; } "
        "hàm main() { f(\"x(\", 2); in 1; }",
        "VPP-SYN-031",
        "a parenthesis inside a call-statement string keeps legacy split depth");
    expectSameDiagnosticAsForcedBridge(
        "hàm f(a, b) { trả về a + b; } "
        "hàm main() { gọi f(\"x(\", 2); in 1; }",
        "VPP-SYN-031",
        "a parenthesis inside an explicit-call string keeps legacy split depth");
}

void testFunctionStatementTerminatorsMatchLegacy() {
    for (const std::string &source : {
             std::string("hàm f() { trả về 7 } hàm main() { in f(); }"),
             std::string("hàm main() { in 7 }"),
             std::string("hàm main() { x = 7 }"),
         }) {
        const std::string error = compileError(source);
        expect(!error.empty(),
               "a non-call function statement without ';' keeps a legacy diagnostic: " +
                   source);
    }

    expectUsesLegacyBridge(
        "hàm f() { trả về 1; } hàm main() { (f()) }",
        "a grouped call without ';' remains in the generic legacy grammar");

    expectDirectMatchesForcedBridge(
        "hàm f() { trả về 1; } hàm main() { f() }",
        "a bare call statement remains semicolon-optional");
}

void testEmitMainCallUsesTheLegacyTextualSlot() {
    expectDirectMatchesForcedBridge(
        "main = 7; in 1;",
        "a variable named main preserves the compatibility auto-call bytecode",
        true);
    expectDirectMatchesForcedBridge(
        "hàm f(main) { trả về main; } in f(7);",
        "a parameter named main preserves the shared textual-slot auto-call contract",
        true);
}

void testDirectConditionalsMatchLegacyJumps() {
    const std::string source =
        "nếu (đúng) { in 1; } hoặc { in 2; }";
    expectDirectMatchesForcedBridge(
        source,
        "structured if/else blocks match the legacy compiler state");

    const CompilerState state = compileState(source);
    expectBytecode(
        state.root,
        {{OP_NEU, 0, 0, 0},
         {OP_BIEN_SO, 1, 0, 0},
         {OP_JUMP_IF_FALSE, 8, 0, 0},
         {OP_MO_KHOI, 0, 0, 0},
         {OP_BIEN_SO, 1, 0, 0},
         {OP_IN, 0, 0, 0},
         {OP_DONG_KHOI, 0, 0, 0},
         {OP_JUMP, 12, 0, 0},
         {OP_MO_KHOI, 0, 0, 0},
         {OP_BIEN_SO, 2, 0, 0},
         {OP_IN, 0, 0, 0},
         {OP_DONG_KHOI, 0, 0, 0}},
        "direct conditional emitter patches absolute else/end targets");
    vietvm::compiler::resetCompilationState();

    expectDirectMatchesForcedBridge(
        "nếu (đúng) { nếu (sai) { in 1; } hoặc { in 2; } } "
        "hoặc { in 3; }",
        "nested structured conditionals patch jumps in one output vector");
    expectDirectMatchesForcedBridge(
        "hàm f(x) { nếu (x > 0) { trả về x; } trả về 0; } "
        "hàm main() { in f(2); }",
        "structured conditionals inside functions match legacy function bytecode");
}

void testUnstructuredConditionalsStayOnBridge() {
    for (const std::string &source : {
             std::string("nếu (đúng) in 1;"),
             std::string("nếu (đúng) { in 1; } hoặc in 2;"),
             std::string("nếu (đúng) { in 1; } hoặc nếu (sai) { in 2; }"),
             std::string("nếu (x = 1) { in x; }"),
             std::string("nếu ({\"x\": 1}) { in 1; }"),
             std::string("nếu () { in 1; }"),
         }) {
        expectUsesLegacyBridge(
            source,
            "a conditional outside the structured direct cohort stays on the bridge");
    }
    expectUsesLegacyBridge(
        "hàm f() { trả về 1; } nếu (gọi f()) { in 1; }",
        "explicit-call condition syntax remains on the compatibility grammar");
    expectDirectMatchesForcedBridge(
        "nếu (native_chưa_biết()) { in 1; }",
        "a dynamic-name condition matches legacy call emission");
    expectSameDiagnosticAsForcedBridge(
        "nếu junk (đúng) { in 1; }",
        "VPP-SYN-001",
        "a malformed conditional header retains the legacy opening-paren diagnostic");
}

void testDirectForLoopsMatchLegacyJumps() {
    const std::string source =
        "lặp (i = 0; i < 2; i++) { in i; };";
    expectDirectMatchesForcedBridge(
        source,
        "structured for-loop operands and body match legacy compiler state");
    const CompilerState state = compileState(source);
    expectBytecode(
        state.root,
        {{OP_KHOI_TAO, 0, 0, 0},
         {OP_BIEN_SO, 0, 0, 0},
         {OP_TEN_BIEN_ID, 0, 0, 0},
         {OP_GAN, 0, 0, 0},
         {OP_LAP, 0, 0, 0},
         {OP_DIEU_KIEN, 0, 0, 0},
         {OP_TEN_BIEN_GIA_TRI, 0, 0, 0},
         {OP_BIEN_SO, 2, 0, 0},
         {OP_NHO_HON, 0, 0, 0},
         {OP_JUMP_IF_FALSE, 18, 0, 0},
         {OP_MO_KHOI, 0, 0, 0},
         {OP_TEN_BIEN_GIA_TRI, 0, 0, 0},
         {OP_IN, 0, 0, 0},
         {OP_DONG_KHOI, 0, 0, 0},
         {OP_CAP_NHAT, 0, 0, 0},
         {OP_TEN_BIEN_ID, 0, 0, 0},
         {OP_CONG_MOT, 0, 0, 0},
         {OP_JUMP, 5, 0, 0}},
        "direct loop emitter patches exit/back-edge targets in optimized coordinates");
    vietvm::compiler::resetCompilationState();

    expectDirectMatchesForcedBridge(
        "hàm main() { lặp (i = 0; i < 3; i++) { "
        "nếu (i == 1) { bỏ qua; } in i; } }",
        "loop, nested conditional and continue match legacy function bytecode");
}

void testUnstructuredLoopsStayOnBridge() {
    for (const std::string &source : {
             std::string("lặp (i = 0; i < 2; i++) in i;"),
             std::string("lặp (i += 1; i < 2; i++) { in i; }"),
             std::string("lặp (i = 0; i < 2; i + 1) { in i; }"),
             std::string("lặp (i = 0; i = 1; i++) { in i; }"),
         }) {
        expectUsesLegacyBridge(
            source,
            "a loop outside the exact structured for-block cohort stays on the bridge");
    }
    expectSameErrorAsForcedBridge(
        "lặp (i = \"x;\"; i != \"\"; i = \"(\") { in i; }",
        "loop-header strings retain the quote-unaware legacy splitter behavior");
    expectDirectMatchesForcedBridge(
        "lặp (i = 0; native_chưa_biết(); i++) { in i; }",
        "a dynamic-name loop condition matches legacy call emission");
}

void testContinueRequiresItsExactLegacyStatementShape() {
    expectDirectMatchesForcedBridge(
        "bỏ qua;",
        "standalone continue retains its exact legacy bytecode");
    expectDirectMatchesForcedBridge(
        "bỏ qua",
        "semicolon-free standalone continue retains its legacy bytecode");
    expectUsesLegacyBridge(
        "bỏ qua 1;",
        "tokens after continue remain owned by legacy statement dispatch");
}

void testRepeatedLoopsAndNestedConditionsMatchLegacy() {
    expectDirectMatchesForcedBridge(
        "lặp (i = 1; i <= 3; i = i + 1) { "
        "nếu (i == 2 || i == 3) { in \"prime: \" + i; } }\n"
        "lặp (i = 1; i <= 3; i = i + 1) { "
        "nếu (i == 1 || i == 3) { in \"value: \" + i; } }",
        "repeated loop slot reuse and nested conditional offsets match legacy");
}

void testDirectSwitchArmsMatchLegacyState() {
    const std::string source =
        "needle = 2; x = 2; "
        "chọn (x) { "
        "ca 1: { in 1; } "
        "ca needle: { in 2; thoát; } "
        "ca \"two\\nlines\": { in 3; } "
        "mặc định: { in 0; } "
        "}";
    expectDirectMatchesForcedBridge(
        source,
        "structured integer/name/string/default switch arms match legacy state");

    const CompilerState state = compileState(source);
    expect(state.backend == vietvm::compiler::BytecodeBackend::DirectIr,
           "a fully structured switch selects the direct IR backend");
    const auto switchOpcode = std::find_if(
        state.root.begin(), state.root.end(),
        [](const Instruction &instruction) { return instruction.op == OP_CHON; });
    expect(switchOpcode != state.root.end(),
           "direct switch emits OP_CHON after evaluating its selector");
    if (switchOpcode != state.root.end()) {
        const std::size_t index = static_cast<std::size_t>(
            std::distance(state.root.begin(), switchOpcode));
        expect(index + 1 < state.root.size() &&
                   state.root[index + 1].op == OP_CA &&
                   state.root[index + 1].operand == 1 &&
                   state.root[index + 1].operandIndex == -1,
               "integer switch label uses the legacy OP_CA encoding");
    }
    expect(std::any_of(
               state.root.begin(), state.root.end(),
               [](const Instruction &instruction) {
                   return instruction.op == OP_CA &&
                          instruction.operandIndex == -2;
               }),
           "name switch label carries a VM slot in OP_CA");
    expect(std::any_of(
               state.root.begin(), state.root.end(),
               [](const Instruction &instruction) {
                   return instruction.op == OP_THOAT;
               }),
           "break inside a switch arm remains OP_THOAT");
    expect(state.pool.size() == 1 && state.pool.front() == "two\nlines",
           "string switch labels decode source escapes exactly once");
    vietvm::compiler::resetCompilationState();
}

void testDirectSwitchInsideFunctionAndNestedSwitch() {
    expectDirectMatchesForcedBridge(
        "X = 1; chọn (X) { ca X: { in 1; } mặc định: { in 0; } }",
        "case-name normalization retains the legacy case-slot identity");
    expectDirectMatchesForcedBridge(
        "hàm chọn số(x) { "
        "  chọn (x) { "
        "    ca 1: { chọn (x + 1) { ca 2: { trả về 10; } "
        "                              mặc định: { trả về 11; } } } "
        "    mặc định: { trả về 0; } "
        "  } "
        "  trả về 99; "
        "} "
        "hàm main() { in chọn số(1); }",
        "nested structured switches inside a function match legacy bytecode");

    expectDirectMatchesForcedBridge(
        "x = 1; chọn (x) { ca 1 { nếu (đúng) { thoát } } }",
        "optional case colon and semicolon-free nested break match legacy state");
    expectDirectMatchesForcedBridge(
        "x = 7; chọn (x) { ca mặc định: { in 0; } }",
        "case-prefixed default arm retains its legacy encoding");
    expectDirectMatchesForcedBridge(
        "x = 7; chọn (x) { mặc định { in 0; } }",
        "default-only switch with optional colon matches legacy state");
    expectDirectMatchesForcedBridge(
        "x = 7; chọn (x) { ca 1: { in 1; } }",
        "a switch without a default arm remains directly representable");
}

void testUnstructuredSwitchesAndBreakStayOnBridge() {
    for (const std::string &source : {
             std::string("chọn (x = 1) { ca 1: { in 1; } }"),
             std::string("chọn ({\"x\": 1}) { ca 1: { in 1; } }"),
             std::string("chọn (x) { ca 1.5: { in 1; } }"),
             std::string("chọn (x) { ca đúng: { in 1; } }"),
             std::string("chọn (x) { ca sai: { in 1; } }"),
             std::string("chọn (x) { ca rỗng: { in 1; } }"),
             std::string("chọn (x) { thoát; ca 1: { in 1; } }"),
             std::string("chọn (x) { ca 1: { thoát 1; } }"),
             std::string("thoát;"),
         }) {
        expectUsesLegacyBridge(
            source,
            "switch syntax outside the exact structured cohort stays on the bridge");
    }
    for (const std::string &source : {
             std::string("chọn (x) { ca -1: { in 1; } }"),
             std::string("chọn (x) { ca 1 + 2: { in 1; } }"),
             std::string("chọn (x) { ca 1: in 1; }"),
         }) {
        expectSameErrorAsForcedBridge(
            source,
            "malformed or trailing switch tokens retain the legacy diagnostic: " +
                source);
    }
}

void testDirectTryCatchThrowMatchesLegacyState() {
    const std::string source =
        "hàm main() { "
        "  thử { ném \"boom\"; in 9; } bắt lỗi (e) { in e; } "
        "  thử { in \"ok\"; } bắt lỗi { in \"bad\"; } "
        "}";
    expectDirectMatchesForcedBridge(
        source,
        "structured try/catch binding and throw match legacy compiler state");

    const CompilerState state = compileState(source);
    expect(state.backend == vietvm::compiler::BytecodeBackend::DirectIr,
           "structured try/catch selects the direct IR backend");
    const auto function = state.functions.find(0);
    expect(function != state.functions.end(),
           "try/catch test emits main function bytecode");
    if (function != state.functions.end()) {
        const auto &code = function->second;
        expect(std::count_if(code.begin(), code.end(), [](const Instruction &inst) {
                   return inst.op == OP_THU;
               }) == 2,
               "each structured try emits one OP_THU frame");
        expect(std::any_of(code.begin(), code.end(), [](const Instruction &inst) {
                   return inst.op == OP_BAT_LOI && inst.operandIndex >= 0;
               }),
               "bound catch emits OP_BAT_LOI with a runtime slot");
        expect(std::any_of(code.begin(), code.end(), [](const Instruction &inst) {
                   return inst.op == OP_BAT_LOI && inst.operandIndex == -1;
               }),
               "unbound catch emits OP_BAT_LOI with the legacy sentinel");
    }
    vietvm::compiler::resetCompilationState();
}

void testNestedTryAndEmptyThrowMatchLegacyFixups() {
    expectDirectMatchesForcedBridge(
        "hàm main() { "
        "  thử { "
        "    thử { ném 7; } bắt lỗi (inner) { ném inner; } "
        "  } bắt lỗi (outer) { in outer; } "
        "}",
        "nested try/catch/rethrow patches absolute handler targets exactly");

    expectDirectMatchesForcedBridge(
        "hàm main() { thử { ném; } bắt lỗi (e) { in e; } }",
        "empty throw preserves the legacy default exception value and pool order");
    const CompilerState emptyThrow = compileState(
        "hàm main() { thử { ném; } bắt lỗi (e) { in e; } }");
    expect(emptyThrow.pool.size() >= 2 &&
               emptyThrow.pool[1] == "lỗi không xác định",
           "empty throw stores the stable unknown-error text after function name");
    vietvm::compiler::resetCompilationState();
}

void testTryCatchCompatibilityEdgesStayOnBridge() {
    expectUsesLegacyBridge(
        "thử { in 1; }",
        "try without a catch remains on legacy statement handling");
    expectUsesLegacyBridge(
        "thử { in 1; } bắt lỗi (e x) { in 2; }",
        "multi-token catch binding remains on the legacy header parser");
    expectUsesLegacyBridge(
        "hàm f() { trả về 1; } hàm main() { "
        "thử { in 1; } bắt lỗi (f) { in f; } }",
        "catch binding colliding with a function keeps legacy name precedence");
    expectDirectMatchesForcedBridge(
        "hàm main() { thử { ném native_chưa_biết(); } bắt lỗi { in 1; } }",
        "dynamic throw expressions match the legacy name fallback");
    expectUsesLegacyBridge(
        "hàm f() { trả về 1; } hàm main() { "
        "thử { ném gọi f(); } bắt lỗi { in 1; } }",
        "explicit-call throw expressions keep legacy grammar behavior");
    expect(!compileError(
               "hàm main() { thử { ném \"x\" } bắt lỗi { in 1; } }").empty(),
           "throw without a semicolon inside a block retains a legacy error");
    for (const std::string &source : {
             std::string("hàm main() { thử { ném ,; } bắt lỗi (e) { in e; } }"),
             std::string("hàm main() { thử { ném []; } bắt lỗi (e) { in e; } }"),
         }) {
        expect(!compileError(source).empty(),
               "invalid throw payload retains its legacy diagnostic");
    }
    for (const std::string &source : {
             std::string("hàm main() { thử { ném (); } bắt lỗi (e) { in e; } }"),
             std::string("hàm main() { thử { ném +; } bắt lỗi (e) { in e; } }"),
         }) {
        expectUsesLegacyBridge(
            source,
            "an unparsable throw payload cannot be reinterpreted as empty throw");
    }
}

void testDirectClassMethodsMatchLegacyState() {
    const std::string source =
        "lớp công khai Toan { "
        "  hàm riêng tư nhan(a, b) { trả về a * b; }; "
        "  hàm công khai tinh(a, b) { trả về nhan(a, b) + 1; }; "
        "}; "
        "hàm main() { in Toan.tinh(2, 3); };";
    expectDirectMatchesForcedBridge(
        source,
        "methods-only class, qualified public call and internal private call "
        "match legacy state");

    const CompilerState state = compileState(source);
    expect(state.backend == vietvm::compiler::BytecodeBackend::DirectIr,
           "a methods-only class selects the direct IR backend");
    expect(state.pool.size() >= 3 && state.pool[0] == "main" &&
               state.pool[1] == "Toan.nhan" &&
               state.pool[2] == "Toan.tinh",
           "top-level function names are predeclared before source-order class methods");
    expect(state.root.size() >= 3 && state.root[0].op == OP_HAM &&
               state.root[0].operand == 1 && state.root[0].operandIndex == 1 &&
               state.root[1].op == OP_HAM && state.root[1].operand == 2 &&
               state.root[1].operandIndex == 2 &&
               state.root[2].op == OP_HAM && state.root[2].operand == 0 &&
               state.root[2].operandIndex == 0,
           "class methods emit OP_HAM in class order after top-level IDs were allocated");
    vietvm::compiler::resetCompilationState();
}

void testClassMethodResolutionTimingMatchesLegacy() {
    expectDirectMatchesForcedBridge(
        "lớp C { "
        "  hàm first(x) { trả về x; } "
        "  hàm second(x) { trả về first(x) + 1; } "
        "} hàm main() { in C.second(2); }",
        "a method may directly call a method registered earlier in its class");

    expectDirectMatchesForcedBridge(
        "lớp C { "
        "  hàm rec(n) { nếu (n <= 0) { trả về 0; } trả về rec(n - 1); } "
        "} hàm main() { in C.rec(2); }",
        "a method is registered before its own body for self recursion");

    expectUsesLegacyBridge(
        "lớp C { "
        "  hàm first() { trả về second(); } "
        "  hàm second() { trả về 2; } "
        "} hàm main() { in C.first(); }",
        "a call to a later class method preserves registry-timing fallback behavior");
    expectUsesLegacyBridge(
        "hàm main() { in C.m(); } lớp C { hàm m() { trả về 1; } }",
        "a qualified method call before its class is emitted stays on the bridge");
    expectUsesLegacyBridge(
        "x = 0; C.m = 1; lớp C { hàm m() { trả về 2; } }",
        "a store to a not-yet-emitted method name keeps legacy ID reuse timing");
    expectUsesLegacyBridge(
        "lớp C { hàm f() { trả về missing(\"C.missing\"); } }",
        "a method dynamic call stays on the bridge when argument pool effects can "
        "change legacy class qualification");
    expectUsesLegacyBridge(
        "hàm m(x) { trả về x; } "
        "lớp C { hàm f() { trả về m(\"C.m\"); } }",
        "a global call from a method stays on the bridge when legacy context can "
        "hijack it after argument emission");
    expectUsesLegacyBridge(
        "lớp C { "
        "  hàm callback(x) { trả về x; } "
        "  hàm f(callback) { trả về callback(1); } "
        "}",
        "a lexical callback colliding with an earlier raw method name keeps "
        "legacy class-method precedence");
}

void testMalformedClassesStayOnBridge() {
    expectDirectMatchesForcedBridge(
        "lớp Empty {}; in 1;",
        "an empty methods-only class emits no class opcode and remains direct");
    expectSameDiagnosticAsForcedBridge(
        "lớp C { x = 1; }",
        "VPP-SYN-014",
        "a class field retains the methods-only legacy diagnostic");
    expectSameDiagnosticAsForcedBridge(
        "lớp C { công khai hàm m() { trả về 1; } }",
        "VPP-SYN-011",
        "modifier-before-function syntax retains the legacy class diagnostic");
}

void testUnsupportedFunctionDefaultsKeepLegacyDiagnostic() {
    expectSameDiagnosticAsForcedBridge(
        "hàm f(a = \"x,y\") { trả về a; }",
        "VPP-SEM-003",
        "a comma inside a string default retains the current legacy header diagnostic");
}

void testFunctionParameterCollisionStaysOnBridge() {
    vietvm::compiler::resetCompilationState();
    const auto artifacts = vietvm::compiler::compilePipeline(
        "hàm f(f) { trả về f; } hàm main() { in f(42); }",
        keywordMap,
        false);
    expect(artifacts.backend ==
               vietvm::compiler::BytecodeBackend::LegacyTokenBridge &&
               artifacts.legacyFallbackRegions > 0,
           "a parameter colliding with a function name keeps legacy lookup behavior");
    vietvm::compiler::resetCompilationState();
}

void testDirectLambdaAndIndirectCallsMatchLegacyState() {
    expectDirectMatchesForcedBridge(
        "f = hàm(x = 2) { trả về x; }; in f();",
        "an assigned structured lambda matches legacy anonymous-function state");

    const CompilerState simple =
        compileState("f = hàm(x = 2) { trả về x; }; in f();");
    expect(simple.backend == vietvm::compiler::BytecodeBackend::DirectIr,
           "a capture-free assigned lambda selects direct IR");
    expect(simple.functionNames.empty(),
           "an anonymous lambda does not enter the function-name registry");
    expect(simple.functions.size() == 1 && simple.functions.count(0) == 1,
           "an anonymous lambda owns function bytecode at its emission-time ID");
    expect(std::none_of(simple.root.begin(), simple.root.end(),
                        [](const Instruction &instruction) {
                            return instruction.op == OP_HAM;
                        }),
           "an anonymous lambda emits no OP_HAM declaration");
    expect(simple.pool == std::vector<std::string>({"i:2"}),
           "lambda defaults enter StringPool without a synthetic function name");
    vietvm::compiler::resetCompilationState();

    expectDirectMatchesForcedBridge(
        "f = hàm(a = 1, b = 1.5, c = \"x\", d = đúng, "
        "e = sai, n = rỗng) { trả về a; };",
        "all primitive lambda defaults preserve legacy encoding and pool order");
    expectDirectMatchesForcedBridge(
        "x = 9; f = hàm(x) { trả về x; }; in f(2);",
        "a lambda parameter reuses an earlier shared textual slot");
    expectDirectMatchesForcedBridge(
        "hàm make() { f = hàm(x) { trả về x + 1; }; trả về f; } "
        "hàm main() { callback = make(); in callback(2); }",
        "a lambda emitted inside a named function retains global slot and ID order");

    const std::string fixture =
        "nhan_doi = hàm(x) { trả về x * 2; };\n"
        "in nhan_doi(21);\n"
        "hàm ap_dung(f, x) { trả về f(x); }\n"
        "in ap_dung(nhan_doi, 5);\n"
        "hàm cong_mac_dinh(a, b = 10) { trả về a + b; }\n"
        "in cong_mac_dinh(5);\n"
        "in cong_mac_dinh(5, 2);";
    expectDirectMatchesForcedBridge(
        fixture,
        "the legacy lambda/HOF/default fixture matches complete compiler state");

    const CompilerState fixtureState = compileState(fixture);
    expect(fixtureState.pool ==
               std::vector<std::string>({"ap_dung", "cong_mac_dinh", "i:10"}),
           "named functions are predeclared before lambda/default pool entries");
    expect(fixtureState.functionNames.size() == 2 &&
               fixtureState.functionNames.count(0) == 1 &&
               fixtureState.functionNames.count(1) == 1 &&
               fixtureState.functionNames.count(2) == 0,
           "lambda ID 2 remains anonymous beside named function IDs 0 and 1");
    expect(fixtureState.functions.size() == 3 &&
               fixtureState.functions.count(2) == 1,
           "fixture emits the lambda body as the third function body");
    vietvm::compiler::resetCompilationState();
}

void testUnsupportedLambdaShapesStayOnBridge() {
    expectUsesLegacyBridge(
        "hàm outer(x) { f = hàm() { trả về x; }; trả về f; }",
        "a lambda capturing an outer parameter remains on the bridge");
    expectUsesLegacyBridge(
        "outer = hàm() { inner = hàm() { trả về 1; }; trả về inner; };",
        "nested lambda allocation remains on the bridge in the first cohort");
    expectUsesLegacyBridge(
        "hàm x() { trả về 1; } f = hàm(x) { trả về x; };",
        "lambda parameters colliding with named functions preserve legacy lookup");
    expectUsesLegacyBridge(
        "f = (hàm() { trả về 1; });",
        "grouping cannot widen an assigned lambda into new direct semantics");
    expectUsesLegacyBridge(
        "f = hàm() { trả về 1; } + 2;",
        "tokens after a lambda body remain owned by compatibility parsing");
    expectUsesLegacyBridge(
        "hàm apply(f) { trả về f(); } "
        "in apply(hàm() { trả về 1; });",
        "a lambda nested in a call argument stays outside the initial direct cohort");
    expectSameDiagnosticAsForcedBridge(
        "f = hàm(x = \"a,b\") { trả về x; };",
        "VPP-SEM-003",
        "a comma inside a lambda string default retains legacy header splitting");
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
    testDirectDynamicAndIndirectCallsMatchLegacyState();
    testDirectFunctionsParametersReturnsAndCalls();
    testDirectMultiwordFunctionsMatchLegacy();
    testExplicitCallOnlyUsesItsStatementGrammar();
    testCallStatementMustConsumeTheWholeExpression();
    testStatementLeadingKeywordCallsStayOnBridge();
    testCallStatementArgumentSplittingMatchesLegacy();
    testFunctionStatementTerminatorsMatchLegacy();
    testEmitMainCallUsesTheLegacyTextualSlot();
    testDirectConditionalsMatchLegacyJumps();
    testUnstructuredConditionalsStayOnBridge();
    testDirectForLoopsMatchLegacyJumps();
    testUnstructuredLoopsStayOnBridge();
    testContinueRequiresItsExactLegacyStatementShape();
    testRepeatedLoopsAndNestedConditionsMatchLegacy();
    testDirectSwitchArmsMatchLegacyState();
    testDirectSwitchInsideFunctionAndNestedSwitch();
    testUnstructuredSwitchesAndBreakStayOnBridge();
    testDirectTryCatchThrowMatchesLegacyState();
    testNestedTryAndEmptyThrowMatchLegacyFixups();
    testTryCatchCompatibilityEdgesStayOnBridge();
    testDirectClassMethodsMatchLegacyState();
    testClassMethodResolutionTimingMatchesLegacy();
    testMalformedClassesStayOnBridge();
    testUnsupportedFunctionDefaultsKeepLegacyDiagnostic();
    testFunctionParameterCollisionStaysOnBridge();
    testDirectLambdaAndIndirectCallsMatchLegacyState();
    testUnsupportedLambdaShapesStayOnBridge();
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
