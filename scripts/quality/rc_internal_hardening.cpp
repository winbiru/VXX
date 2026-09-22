#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "common/storeString.h"
#include "vpp/bytecode/verifier.h"
#include "vpp/compiler/codegen.h"
#include "vpp/compiler/ir.h"

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

vietvm::bytecode::BytecodeVerificationContext verificationContext(
    std::size_t stringPoolSize,
    std::unordered_set<int> functionIds = {}) {
    vietvm::bytecode::BytecodeVerificationContext result;
    result.stringPoolSize = stringPoolSize;
    result.functionIds = std::move(functionIds);
    return result;
}

void testMalformedAstReferencesBecomeUnsupportedIr() {
    using namespace vietvm::compiler;
    using namespace vietvm::frontend;

    AstProgram program;
    program.tokens.push_back(token("hỏng"));

    AstStatement statement;
    statement.kind = AstStatementKind::Expression;
    statement.tokenEnd = program.tokens.size();
    statement.expressionRoots.push_back(42);
    program.statements.push_back(std::move(statement));

    IrProgram ir = lowerToIr(program, {});
    expect(ir.instructions.size() == 1 &&
               ir.instructions.front().expressionRoots.size() == 1,
           "AST lỗi vẫn hạ thành vùng IR chẩn đoán được");
    if (!ir.instructions.empty() && !ir.instructions.front().expressionRoots.empty()) {
        const IrValue *value = ir.value(ir.instructions.front().expressionRoots.front());
        expect(value != nullptr &&
                   value->opcode == IrValueOpcode::UnsupportedDirectRegion &&
                   value->sourceExprId == 42,
               "ExprId ngoài arena trở thành UnsupportedDirectRegion");
    }
    expect(ir.unsupportedDirectRegionCount == 1,
           "AST lỗi được tính đúng một unsupported-direct region");

    AstProgram cyclicProgram;
    cyclicProgram.tokens.push_back(token("!"));
    AstExpression cyclic;
    cyclic.id = 0;
    cyclic.kind = AstExpressionKind::Unary;
    cyclic.text = "!";
    cyclic.operand = 0;
    cyclicProgram.expressions.push_back(std::move(cyclic));

    AstStatement cyclicStatement;
    cyclicStatement.kind = AstStatementKind::Expression;
    cyclicStatement.tokenEnd = cyclicProgram.tokens.size();
    cyclicStatement.expressionRoots.push_back(0);
    cyclicProgram.statements.push_back(std::move(cyclicStatement));

    IrProgram cyclicIr = lowerToIr(cyclicProgram, {});
    const IrValue *cyclicValue = cyclicIr.value(0);
    expect(cyclicValue != nullptr &&
               cyclicValue->opcode == IrValueOpcode::UnsupportedDirectRegion,
           "chu trình ExprId bị chặn trước codegen");
}

void testInvalidIrIsRejectedBeforeEmission() {
    using namespace vietvm::compiler;

    IrProgram program;
    IrValue invalidBinary;
    invalidBinary.id = 0;
    invalidBinary.opcode = IrValueOpcode::Binary;
    invalidBinary.text = "+";
    invalidBinary.operands = {1, 2};
    program.values.push_back(std::move(invalidBinary));

    IrInstruction print;
    print.opcode = IrOpcode::Print;
    print.expressionRoots.push_back(0);
    program.instructions.push_back(std::move(print));

    const DirectIrSupport support = analyzeDirectIrSupport(program);
    expect(!support.supported && support.unsupportedRegions == 1,
           "IR có operand ngoài arena bị phân loại không hỗ trợ");

    CompilationRegistryState state;
    bool rejected = false;
    try {
        (void)emitDirectBytecode(state, program, {}, true);
    } catch (const std::logic_error &) {
        rejected = true;
    }
    expect(rejected, "direct codegen từ chối IR lỗi trước khi dereference operand");

    IrProgram malformedControlFlow;
    IrInstruction conditional;
    conditional.opcode = IrOpcode::Conditional;
    conditional.conditionalForm = vietvm::frontend::AstConditionalForm::IfBlock;
    malformedControlFlow.instructions.push_back(std::move(conditional));
    const DirectIrSupport controlFlowSupport = analyzeDirectIrSupport(malformedControlFlow);
    expect(!controlFlowSupport.supported,
           "IR điều kiện thiếu expression/body bị từ chối có kiểm soát");
}

void testBytecodeVerifierRejectsMalformedMetadata() {
    using vietvm::bytecode::verifyBytecode;

    const std::vector<Instruction> valid = {
        {OP_HAM, 0, 7, 0},
        {OP_CHUOI, 0, 1, 0},
        {OP_JUMP, 3, 0, 0},
        {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
    };
    expect(!verifyBytecode(valid, verificationContext(2, {7})).has_value(),
           "verifier chấp nhận bytecode hợp lệ");

    expect(verifyBytecode({{999, 0, 0, 0}}, verificationContext(0)).has_value(),
           "verifier từ chối opcode lạ");
    expect(verifyBytecode(
               {{OP_JUMP, 4, 0, 0}, {OP_DUNG_CHUONG_TRINH, 0, 0, 0}},
               verificationContext(0))
               .has_value(),
           "verifier từ chối jump ngoài phạm vi");
    expect(verifyBytecode({{OP_CHUOI, 0, 2, 0}}, verificationContext(2)).has_value(),
           "verifier từ chối StringPool index ngoài phạm vi");
    expect(verifyBytecode({{OP_HAM, 0, 9, 0}}, verificationContext(1, {7})).has_value(),
           "verifier từ chối function id không có bytecode");
    expect(verifyBytecode({{OP_TAO_DONG_BAO, 9, 1, 0}}, verificationContext(0, {7}))
               .has_value(),
           "verifier từ chối closure tham chiếu function id không tồn tại");
}

} // namespace

int main() {
    testMalformedAstReferencesBecomeUnsupportedIr();
    testInvalidIrIsRejectedBeforeEmission();
    testBytecodeVerifierRejectsMalformedMetadata();

    if (failures != 0) {
        std::cerr << failures << " RC internal hardening check(s) failed\n";
        return 1;
    }
    std::cout << "RC internal hardening: PASS\n";
    return 0;
}
