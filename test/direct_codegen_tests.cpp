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
    return {artifacts.bytecode,
            vietvm::compiler::StringPool::getPool(),
            vietvm::compiler::hamMap::hamBytecodeMap,
            vietvm::compiler::hamMap::hamNameIndexMap};
}

void expectDirectCompilationStable(const std::string &source,
                                   const std::string &message,
                                   bool emitMainCall = false) {
    const CompilerState first = compileState(source, emitMainCall);
    const CompilerState second = compileState(source, emitMainCall);
    expect(sameBytecode(first.root, second.root), message + " (root bytecode)");
    expect(first.pool == second.pool, message + " (StringPool)");
    expect(first.functionNames == second.functionNames,
           message + " (function-name registry)");
    expect(first.functions.size() == second.functions.size(),
           message + " (function registry size)");
    for (const auto &entry : first.functions) {
        const auto found = second.functions.find(entry.first);
        expect(found != second.functions.end() &&
                   sameBytecode(entry.second, found->second),
               message + " (function " + std::to_string(entry.first) + " bytecode)");
    }
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

void expectRejectsUnsupportedDirectIr(const std::string &source,
                                      const std::string &message) {
    expect(!compileError(source).empty(), message);
}

void expectCompileDiagnostic(const std::string &source,
                             const std::string &message) {
    expect(!compileError(source).empty(), message + " (pipeline diagnostic)");
}

void testDirectLiteralAndBinaryPrint() {
    vietvm::compiler::resetCompilationState();
    const auto artifacts = vietvm::compiler::compilePipeline(
        "in 1 + 2 * 3;", keywordMap, true);

    expect(artifacts.unsupportedDirectIrRegions == 0,
           "safe arithmetic print reports zero unsupported direct-IR regions");
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

void testDirectNegativeNumericLiteralsMatchLegacy() {
    expectDirectCompilationStable(
        "in -7; in -3.5;",
        "negative numeric literals use the same merged-literal bytecode as legacy");
}

void testDirectAssignmentsOwnVmSlots() {
    vietvm::compiler::resetCompilationState();
    const auto artifacts = vietvm::compiler::compilePipeline(
        "x = 2; x += 3; in x;", keywordMap, true);

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
    expectDirectCompilationStable(
        "in native_chưa_biết(1);",
        "an unresolved/native name call uses the legacy StringPool fallback",
        true);
    expectDirectCompilationStable(
        "gọi missing(1);",
        "an unresolved explicit call retains its name-based fallback");
    expectDirectCompilationStable(
        "hàm inc(x) { trả về x + 1; } callback = inc; in callback(2);",
        "a variable holding a function id emits an indirect call");
    expectDirectCompilationStable(
        "callback = 1; in callback(2);",
        "an existing textual slot takes the legacy indirect-call path");
    expectDirectCompilationStable(
        "in callback(2); callback = 1;",
        "a call before its textual slot exists retains name-fallback timing");
    expectDirectCompilationStable(
        "in missing(\"arg\") + 1;",
        "nested dynamic call emits arguments before storing its callable name");
    expectDirectCompilationStable(
        "missing()",
        "a semicolon-free standalone dynamic call keeps dedicated-call behavior");
    expectDirectCompilationStable(
        "missing(\"missing\");",
        "a callable name deduplicates against an earlier argument string");
    expectDirectCompilationStable(
        "outer(\"a\", inner(\"b\"));",
        "nested dynamic calls preserve argument and callable-name pool order");
    expectDirectCompilationStable(
        "hàm apply(f, x) { trả về f(x); } "
        "hàm inc(x) { trả về x + 1; } "
        "hàm main() { in apply(inc, 2); }",
        "a callback parameter lowers to the legacy indirect-call sequence");
    expectDirectCompilationStable(
        "hàm seed(callback) { trả về 0; } "
        "hàm use() { trả về callback(1); }",
        "shared textual slots from an earlier function retain legacy call dispatch");
    expectDirectCompilationStable(
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
    expectDirectCompilationStable(
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
    expectDirectCompilationStable(
        source,
        "multiword forward, explicit and nested direct calls match legacy state");
}

void testExplicitCallOnlyUsesItsStatementGrammar() {
    expectRejectsUnsupportedDirectIr(
        "hàm f() { trả về 7; } hàm main() { in gọi f(); }",
        "an explicit call nested under print stays on the legacy grammar");
    expectRejectsUnsupportedDirectIr(
        "hàm f() { trả về 7; } hàm main() { in (gọi f()); }",
        "grouping does not erase the explicit-call grammar marker");
    expectRejectsUnsupportedDirectIr(
        "hàm f() { trả về 7; } hàm main() { trả về gọi f(); }",
        "an explicit call nested under return is rejected by direct IR");
    expectRejectsUnsupportedDirectIr(
        "hàm f() { trả về 7; } hàm g(x) { trả về x; } "
        "hàm main() { g(gọi f()); }",
        "an explicit call used as another call argument is rejected by direct IR");

    expectDirectCompilationStable(
        "hàm f() { trả về 7; } hàm main() { gọi f() }",
        "a standalone explicit call retains its semicolon-optional legacy form");
}

void testCallStatementMustConsumeTheWholeExpression() {
    expectRejectsUnsupportedDirectIr(
        "hàm f(x) { trả về x; } hàm main() { f(1) + 2; in 9; }",
        "a leading call with trailing expression text stays on legacy dispatch");
    expectRejectsUnsupportedDirectIr(
        "hàm f() { trả về 1; } hàm main() { (f()) + 2; }",
        "a grouped call used inside a plain statement expression stays on legacy parsing");
    expectRejectsUnsupportedDirectIr(
        "hàm f() { trả về 1; } hàm main() { !f(); }",
        "a non-root call in a plain statement expression stays on legacy parsing");
}

void testStatementLeadingKeywordCallsRejectUnsupportedForms() {
    expectCompileDiagnostic(
        "hàm gọi() { trả về 7; } hàm main() { gọi(); in 1; }",
        "a normal call named 'gọi' remains owned by the statement keyword handler");
    expectRejectsUnsupportedDirectIr(
        "hàm dừng x() { trả về 7; } hàm main() { dừng x(); in 1; }",
        "a call beginning with an opcode keyword remains on legacy statement dispatch");
}

void testCallStatementArgumentSplittingMatchesLegacy() {
    expectDirectCompilationStable(
        "hàm f(a, b) { trả về a + b; } "
        "hàm main() { f(\"x,y\", \"z\"); in 1; }",
        "a comma inside a normal call-statement string uses shared quote-aware splitting");
    expectDirectCompilationStable(
        "hàm f(a, b) { trả về a + b; } "
        "hàm main() { gọi f(\"x,y\", \"z\"); in 1; }",
        "a comma inside an explicit-call string uses shared quote-aware splitting");
    expectDirectCompilationStable(
        "hàm f(a, b) { trả về a + b; } "
        "hàm main() { f(\"x(\", 2); in 1; }",
        "a parenthesis inside a normal call string does not change split depth");
    expectDirectCompilationStable(
        "hàm f(a, b) { trả về a + b; } "
        "hàm main() { gọi f(\"x(\", 2); in 1; }",
        "a parenthesis inside an explicit-call string does not change split depth");
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

    expectRejectsUnsupportedDirectIr(
        "hàm f() { trả về 1; } hàm main() { (f()) }",
        "a grouped call without ';' remains in the generic legacy grammar");

    expectDirectCompilationStable(
        "hàm f() { trả về 1; } hàm main() { f() }",
        "a bare call statement remains semicolon-optional");
}

void testEmitMainCallUsesTheLegacyTextualSlot() {
    expectDirectCompilationStable(
        "main = 7; in 1;",
        "a variable named main preserves the compatibility auto-call bytecode",
        true);
    expectDirectCompilationStable(
        "hàm f(main) { trả về main; } in f(7);",
        "a parameter named main preserves the shared textual-slot auto-call contract",
        true);
}

void testDirectConditionalsMatchLegacyJumps() {
    const std::string source =
        "nếu (đúng) { in 1; } hoặc { in 2; }";
    expectDirectCompilationStable(
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

    expectDirectCompilationStable(
        "nếu (đúng) { nếu (sai) { in 1; } hoặc { in 2; } } "
        "hoặc { in 3; }",
        "nested structured conditionals patch jumps in one output vector");
    expectDirectCompilationStable(
        "hàm f(x) { nếu (x > 0) { trả về x; } trả về 0; } "
        "hàm main() { in f(2); }",
        "structured conditionals inside functions match legacy function bytecode");
}

void testUnstructuredConditionalsAreRejected() {
    for (const std::string &source : {
             std::string("nếu (đúng) in 1;"),
             std::string("nếu (đúng) { in 1; } hoặc in 2;"),
             std::string("nếu (đúng) { in 1; } hoặc nếu (sai) { in 2; }"),
             std::string("nếu (x = 1) { in x; }"),
             std::string("nếu ({\"x\": 1}) { in 1; }"),
             std::string("nếu () { in 1; }"),
         }) {
        expectRejectsUnsupportedDirectIr(
            source,
            "a conditional outside the structured direct cohort is rejected by direct IR");
    }
    expectRejectsUnsupportedDirectIr(
        "hàm f() { trả về 1; } nếu (gọi f()) { in 1; }",
        "explicit-call condition syntax remains on the compatibility grammar");
    expectDirectCompilationStable(
        "nếu (native_chưa_biết()) { in 1; }",
        "a dynamic-name condition matches legacy call emission");
    expectCompileDiagnostic(
        "nếu junk (đúng) { in 1; }",
        "a malformed conditional header retains the legacy opening-paren diagnostic");
}

void testDirectForLoopsMatchLegacyJumps() {
    const std::string source =
        "lặp (i = 0; i < 2; i++) { in i; };";
    expectDirectCompilationStable(
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

    expectDirectCompilationStable(
        "hàm main() { lặp (i = 0; i < 3; i++) { "
        "nếu (i == 1) { bỏ qua; } in i; } }",
        "loop, nested conditional and continue match legacy function bytecode");
}

void testUnstructuredLoopsAreRejected() {
    for (const std::string &source : {
             std::string("lặp (i = 0; i < 2; i++) in i;"),
             std::string("lặp (i += 1; i < 2; i++) { in i; }"),
             std::string("lặp (i = 0; i < 2; i + 1) { in i; }"),
             std::string("lặp (i = 0; i = 1; i++) { in i; }"),
         }) {
        expectRejectsUnsupportedDirectIr(
            source,
            "a loop outside the exact structured for-block cohort is rejected by direct IR");
    }
    expectDirectCompilationStable(
        "lặp (i = \"x;\"; i != \"\"; i = \"(\") { in i; }",
        "loop-header strings use shared quote-aware top-level splitting");
    expectDirectCompilationStable(
        "lặp (i = 0; native_chưa_biết(); i++) { in i; }",
        "a dynamic-name loop condition matches legacy call emission");
}

void testContinueRequiresItsExactLegacyStatementShape() {
    expectDirectCompilationStable(
        "bỏ qua;",
        "standalone continue retains its exact legacy bytecode");
    expectDirectCompilationStable(
        "bỏ qua",
        "semicolon-free standalone continue retains its legacy bytecode");
    expectRejectsUnsupportedDirectIr(
        "bỏ qua 1;",
        "tokens after continue remain owned by legacy statement dispatch");
}

void testRepeatedLoopsAndNestedConditionsMatchLegacy() {
    expectDirectCompilationStable(
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
    expectDirectCompilationStable(
        source,
        "structured integer/name/string/default switch arms match legacy state");

    const CompilerState state = compileState(source);
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
    expectDirectCompilationStable(
        "X = 1; chọn (X) { ca X: { in 1; } mặc định: { in 0; } }",
        "case-name normalization retains the legacy case-slot identity");
    expectDirectCompilationStable(
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

    expectDirectCompilationStable(
        "x = 1; chọn (x) { ca 1 { nếu (đúng) { thoát } } }",
        "optional case colon and semicolon-free nested break match legacy state");
    expectDirectCompilationStable(
        "x = 7; chọn (x) { ca mặc định: { in 0; } }",
        "case-prefixed default arm retains its legacy encoding");
    expectDirectCompilationStable(
        "x = 7; chọn (x) { mặc định { in 0; } }",
        "default-only switch with optional colon matches legacy state");
    expectDirectCompilationStable(
        "x = 7; chọn (x) { ca 1: { in 1; } }",
        "a switch without a default arm remains directly representable");
}

void testUnstructuredSwitchesAndBreakAreRejected() {
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
        expectRejectsUnsupportedDirectIr(
            source,
            "switch syntax outside the exact structured cohort is rejected by direct IR");
    }
    for (const std::string &source : {
             std::string("chọn (x) { ca -1: { in 1; } }"),
             std::string("chọn (x) { ca 1 + 2: { in 1; } }"),
             std::string("chọn (x) { ca 1: in 1; }"),
         }) {
        expectCompileDiagnostic(
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
    expectDirectCompilationStable(
        source,
        "structured try/catch binding and throw match legacy compiler state");

    const CompilerState state = compileState(source);
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
    expectDirectCompilationStable(
        "hàm main() { "
        "  thử { "
        "    thử { ném 7; } bắt lỗi (inner) { ném inner; } "
        "  } bắt lỗi (outer) { in outer; } "
        "}",
        "nested try/catch/rethrow patches absolute handler targets exactly");

    expectDirectCompilationStable(
        "hàm main() { thử { ném; } bắt lỗi (e) { in e; } }",
        "empty throw preserves the legacy default exception value and pool order");
    const CompilerState emptyThrow = compileState(
        "hàm main() { thử { ném; } bắt lỗi (e) { in e; } }");
    expect(emptyThrow.pool.size() >= 2 &&
               emptyThrow.pool[1] == "lỗi không xác định",
           "empty throw stores the stable unknown-error text after function name");
    vietvm::compiler::resetCompilationState();
}

void testTryCatchCompatibilityEdgesAreRejected() {
    expectRejectsUnsupportedDirectIr(
        "thử { in 1; }",
        "try without a catch is rejected by direct IR statement handling");
    expectRejectsUnsupportedDirectIr(
        "thử { in 1; } bắt lỗi (e x) { in 2; }",
        "multi-token catch binding is rejected by the direct IR header parser");
    expectRejectsUnsupportedDirectIr(
        "hàm f() { trả về 1; } hàm main() { "
        "thử { in 1; } bắt lỗi (f) { in f; } }",
        "catch binding colliding with a function is rejected until direct IR defines the name precedence");
    expectDirectCompilationStable(
        "hàm main() { thử { ném native_chưa_biết(); } bắt lỗi { in 1; } }",
        "dynamic throw expressions match the legacy name fallback");
    expectRejectsUnsupportedDirectIr(
        "hàm f() { trả về 1; } hàm main() { "
        "thử { ném gọi f(); } bắt lỗi { in 1; } }",
        "explicit-call throw expressions are rejected by direct IR grammar handling");
    expect(!compileError(
               "hàm main() { thử { ném \"x\" } bắt lỗi { in 1; } }").empty(),
           "throw without a semicolon inside a block retains a legacy error");
    for (const std::string &source : {
             std::string("hàm main() { thử { ném ,; } bắt lỗi (e) { in e; } }"),
         }) {
        expect(!compileError(source).empty(),
               "invalid throw payload retains its legacy diagnostic");
    }
    expect(compileError(
               "hàm main() { thử { ném []; } bắt lỗi (e) { in e; } }").empty(),
           "a list literal is a valid throw payload");
    for (const std::string &source : {
             std::string("hàm main() { thử { ném (); } bắt lỗi (e) { in e; } }"),
             std::string("hàm main() { thử { ném +; } bắt lỗi (e) { in e; } }"),
         }) {
        expectRejectsUnsupportedDirectIr(
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
    expectDirectCompilationStable(
        source,
        "methods-only class, qualified public call and internal private call "
        "match legacy state");

    const CompilerState state = compileState(source);
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
    expectDirectCompilationStable(
        "lớp C { "
        "  hàm first(x) { trả về x; } "
        "  hàm second(x) { trả về first(x) + 1; } "
        "} hàm main() { in C.second(2); }",
        "a method may directly call a method registered earlier in its class");

    expectDirectCompilationStable(
        "lớp C { "
        "  hàm rec(n) { nếu (n <= 0) { trả về 0; } trả về rec(n - 1); } "
        "} hàm main() { in C.rec(2); }",
        "a method is registered before its own body for self recursion");

    expectRejectsUnsupportedDirectIr(
        "lớp C { "
        "  hàm first() { trả về second(); } "
        "  hàm second() { trả về 2; } "
        "} hàm main() { in C.first(); }",
        "a call to a later class method preserves registry-timing fallback behavior");
    expectRejectsUnsupportedDirectIr(
        "hàm main() { in C.m(); } lớp C { hàm m() { trả về 1; } }",
        "a qualified method call before its class is emitted is rejected by direct IR");
    expectRejectsUnsupportedDirectIr(
        "x = 0; C.m = 1; lớp C { hàm m() { trả về 2; } }",
        "a store to a not-yet-emitted method name is rejected until direct IR defines ID reuse timing");
    expectDirectCompilationStable(
        "lớp C { hàm f() { trả về missing(\"C.missing\"); } }",
        "a method dynamic call preserves argument-driven class qualification");
    expectDirectCompilationStable(
        "hàm m(x) { trả về x; } "
        "lớp C { hàm f() { trả về m(\"C.m\"); } }",
        "a global call from a method preserves legacy post-argument context lookup");
    expectDirectCompilationStable(
        "lớp C { "
        "  hàm callback(x) { trả về x; } "
        "  hàm f(callback) { trả về callback(1); } "
        "}",
        "a lexical callback collision preserves legacy class-method precedence");
}

void testMalformedClassesReportDiagnostics() {
    expectDirectCompilationStable(
        "lớp Empty {}; in 1;",
        "an empty methods-only class emits no class opcode and remains direct");
    expectCompileDiagnostic(
        "lớp C { x = 1; }",
        "a class field retains the methods-only legacy diagnostic");
    expectCompileDiagnostic(
        "lớp C { công khai hàm m() { trả về 1; } }",
        "modifier-before-function syntax retains the legacy class diagnostic");
}

void testFunctionDefaultsUseDirectIr() {
    expectDirectCompilationStable(
        "hàm f(a = \"x,y\") { trả về a; }",
        "a comma inside a function string default uses shared parameter splitting");
}

void testFunctionParameterCollisionIsRejectedByDirectIr() {
    expectRejectsUnsupportedDirectIr(
        "hàm f(f) { trả về f; } hàm main() { in f(42); }",
        "a parameter colliding with a function name is rejected until direct lookup semantics support it");
}

void testDirectLambdaAndIndirectCallsMatchLegacyState() {
    expectDirectCompilationStable(
        "f = hàm(x = 2) { trả về x; }; in f();",
        "an assigned structured lambda matches legacy anonymous-function state");

    const CompilerState simple =
        compileState("f = hàm(x = 2) { trả về x; }; in f();");
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

    expectDirectCompilationStable(
        "f = hàm(a = 1, b = 1.5, c = \"x\", d = đúng, "
        "e = sai, n = rỗng) { trả về a; };",
        "all primitive lambda defaults preserve legacy encoding and pool order");
    expectDirectCompilationStable(
        "x = 9; f = hàm(x) { trả về x; }; in f(2);",
        "a lambda parameter reuses an earlier shared textual slot");
    expectDirectCompilationStable(
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
    expectDirectCompilationStable(
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

void testUnsupportedLambdaShapesAreRejected() {
    expectRejectsUnsupportedDirectIr(
        "hàm outer(x) { f = hàm() { trả về x; }; trả về f; }",
        "a lambda capturing an outer parameter is rejected by direct IR");
    expectRejectsUnsupportedDirectIr(
        "outer = hàm() { inner = hàm() { trả về 1; }; trả về inner; };",
        "nested lambda allocation is rejected by direct IR in the first cohort");
    expectRejectsUnsupportedDirectIr(
        "hàm x() { trả về 1; } f = hàm(x) { trả về x; };",
        "lambda parameters colliding with named functions are rejected until direct IR defines lookup semantics");
    expectRejectsUnsupportedDirectIr(
        "f = (hàm() { trả về 1; });",
        "grouping cannot widen an assigned lambda into new direct semantics");
    expectRejectsUnsupportedDirectIr(
        "f = hàm() { trả về 1; } + 2;",
        "tokens after a lambda body remain rejected by direct IR parsing");
    expectRejectsUnsupportedDirectIr(
        "hàm apply(f) { trả về f(); } "
        "in apply(hàm() { trả về 1; });",
        "a lambda nested in a call argument is rejected until direct IR supports this shape");
    expectDirectCompilationStable(
        "f = hàm(x = \"a,b\") { trả về x; };",
        "a comma inside a lambda string default uses shared parameter splitting");
}

void testPrimitiveMapUsesDirectIrAndLegacyEncoding() {
    vietvm::compiler::resetCompilationState();
    const auto artifacts = vietvm::compiler::compilePipeline(
        "m = {\"a\": 1, \"b\": \"x\", \"c\": rỗng}; in m;", keywordMap, true);

    const auto &pool = vietvm::compiler::StringPool::getPool();
    const std::string expectedEncoding =
        std::string("a\x1f" "i\x1f" "1\x1e" "b\x1f" "s\x1f" "x\x1e" "c\x1f" "n\x1f");
    expect(!pool.empty() && pool.front() == expectedEncoding,
           "direct map emitter preserves the VM RS/FS StringPool contract");
    expect(!artifacts.bytecode.empty() && artifacts.bytecode.front().op == OP_MAP_LITERAL &&
               artifacts.bytecode.front().operandIndex == 0,
           "direct map emitter produces OP_MAP_LITERAL with the encoded pool index");
    vietvm::compiler::resetCompilationState();
}

void testPrimitiveListUsesDirectIrAndLegacyEncoding() {
    vietvm::compiler::resetCompilationState();
    const auto artifacts = vietvm::compiler::compilePipeline(
        "ds = [1, \"x\", rỗng]; ds[1] = \"y\"; in ds[1];", keywordMap, true);

    const auto &pool = vietvm::compiler::StringPool::getPool();
    const std::string expectedEncoding =
        std::string("i\x1f" "1\x1e" "s\x1f" "x\x1e" "n\x1f");
    expect(!pool.empty() && pool.front() == expectedEncoding,
           "direct list emitter preserves the VM RS/FS StringPool contract before later string operands");
    expect(!artifacts.bytecode.empty() && artifacts.bytecode.front().op == OP_LIST_LITERAL &&
               artifacts.bytecode.front().operandIndex == 0,
           "direct list emitter produces OP_LIST_LITERAL with the encoded pool index");
    expect(std::any_of(artifacts.bytecode.begin(), artifacts.bytecode.end(),
                       [](const Instruction &instruction) {
                           return instruction.op == OP_DOC_CHI_SO;
                       }),
           "direct list index emits OP_DOC_CHI_SO");
    expect(std::any_of(artifacts.bytecode.begin(), artifacts.bytecode.end(),
                       [](const Instruction &instruction) {
                           return instruction.op == OP_GAN_CHI_SO;
                       }),
           "direct list index assignment emits OP_GAN_CHI_SO");
    vietvm::compiler::resetCompilationState();
}

void testNestedListUsesSharedLiteralWireAndChainedIndexing() {
    vietvm::compiler::resetCompilationState();
    const auto artifacts = vietvm::compiler::compilePipeline(
        "ma trận = [[1, 2], [3, 4]]; ma trận[0][1] = 9; in ma trận[1][0];",
        keywordMap,
        true);

    const auto &pool = vietvm::compiler::StringPool::getPool();
    const std::string expectedEncoding =
        std::string("l\x1f" "i\\f1\\ei\\f2\x1e" "l\x1f" "i\\f3\\ei\\f4");
    expect(pool.size() == 1 && pool.front() == expectedEncoding,
           "nested list fields use the shared escaped literal-wire format");
    expect(std::count_if(artifacts.bytecode.begin(), artifacts.bytecode.end(),
                         [](const Instruction &instruction) {
                             return instruction.op == OP_DOC_CHI_SO;
                         }) == 3,
           "nested read/write emits one index opcode for each index segment");
    expect(std::count_if(artifacts.bytecode.begin(), artifacts.bytecode.end(),
                         [](const Instruction &instruction) {
                             return instruction.op == OP_GAN_CHI_SO;
                         }) == 1,
           "nested list write emits OP_GAN_CHI_SO once");
    vietvm::compiler::resetCompilationState();
}

void testMapEscapesUseStableDirectEncoding() {
    const std::string source =
        R"VPP(m = {"line\nkey": "x\ny", "quote": "a\"b", "slash": "c\\d"}; in m;)VPP";

    vietvm::compiler::resetCompilationState();
    (void)vietvm::compiler::compilePipeline(
        source, keywordMap, false);
    const std::vector<std::string> directPool =
        vietvm::compiler::StringPool::getPool();

    const std::string expectedEncoding =
        std::string("line\\nkey\x1f" "s\x1f" "x\\ny\x1e") +
        "quote\x1f" "s\x1f" "a\"b\x1e" +
        "slash\x1f" "s\x1f" "c\\\\d";
    expect(directPool.size() == 1 && directPool.front() == expectedEncoding,
           "map strings decode source escapes before applying RS/FS escaping");
    vietvm::compiler::resetCompilationState();
}

void testMapGrammarAndContextReportDiagnostics() {
    expectCompileDiagnostic(
        "m = {hello world: 1};",
        "a contextual multi-word name is not accepted as one legacy map key");
    expectCompileDiagnostic(
        "in !{\"a\": 1};",
        "a map nested under a unary operator remains a legacy diagnostic");
    expectCompileDiagnostic(
        "m = {\"a\": 1} + 2;",
        "a map nested under a binary assignment RHS remains a legacy diagnostic");
    expectCompileDiagnostic(
        "in ({\"a\": 1});",
        "a parenthesized map remains outside the legacy whole-map grammar");
    expectCompileDiagnostic(
        "m = {(\"a\"): 1};",
        "a parenthesized key remains outside the legacy map-key grammar");
    expectCompileDiagnostic(
        "m = {\"a\": (1)};",
        "a parenthesized value remains outside the legacy primitive-value grammar");
}

void testGroupedStoresReportDiagnostics() {
    for (const std::string &source : {
             std::string("(x) = 1;"),
             std::string("(x)++;"),
             std::string("(x = 1);"),
             std::string("(x += 1);"),
             std::string("x = 0; (x++);"),
         }) {
        expectCompileDiagnostic(
            source,
            "a grouped store keeps the legacy mismatched-parentheses diagnostic");
    }
}

} // namespace

int main() {
    testDirectLiteralAndBinaryPrint();
    testDirectNegativeNumericLiteralsMatchLegacy();
    testDirectAssignmentsOwnVmSlots();
    testDirectDynamicAndIndirectCallsMatchLegacyState();
    testDirectFunctionsParametersReturnsAndCalls();
    testDirectMultiwordFunctionsMatchLegacy();
    testExplicitCallOnlyUsesItsStatementGrammar();
    testCallStatementMustConsumeTheWholeExpression();
    testStatementLeadingKeywordCallsRejectUnsupportedForms();
    testCallStatementArgumentSplittingMatchesLegacy();
    testFunctionStatementTerminatorsMatchLegacy();
    testEmitMainCallUsesTheLegacyTextualSlot();
    testDirectConditionalsMatchLegacyJumps();
    testUnstructuredConditionalsAreRejected();
    testDirectForLoopsMatchLegacyJumps();
    testUnstructuredLoopsAreRejected();
    testContinueRequiresItsExactLegacyStatementShape();
    testRepeatedLoopsAndNestedConditionsMatchLegacy();
    testDirectSwitchArmsMatchLegacyState();
    testDirectSwitchInsideFunctionAndNestedSwitch();
    testUnstructuredSwitchesAndBreakAreRejected();
    testDirectTryCatchThrowMatchesLegacyState();
    testNestedTryAndEmptyThrowMatchLegacyFixups();
    testTryCatchCompatibilityEdgesAreRejected();
    testDirectClassMethodsMatchLegacyState();
    testClassMethodResolutionTimingMatchesLegacy();
    testMalformedClassesReportDiagnostics();
    testFunctionDefaultsUseDirectIr();
    testFunctionParameterCollisionIsRejectedByDirectIr();
    testDirectLambdaAndIndirectCallsMatchLegacyState();
    testUnsupportedLambdaShapesAreRejected();
    testPrimitiveMapUsesDirectIrAndLegacyEncoding();
    testPrimitiveListUsesDirectIrAndLegacyEncoding();
    testNestedListUsesSharedLiteralWireAndChainedIndexing();
    testMapEscapesUseStableDirectEncoding();
    testMapGrammarAndContextReportDiagnostics();
    testGroupedStoresReportDiagnostics();

    if (failures != 0) {
        std::cerr << failures << " direct codegen unit test(s) failed\n";
        return 1;
    }
    std::cout << "direct codegen unit tests passed\n";
    return 0;
}
