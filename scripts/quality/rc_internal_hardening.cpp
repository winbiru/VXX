#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "common/storeString.h"
#include "frontend/keywords.h"
#include "vpp/bytecode/intrinsic.h"
#include "vpp/bytecode/opcode.h"
#include "vpp/bytecode/verifier.h"
#include "vpp/compiler/codegen.h"
#include "vpp/compiler/ir.h"
#include "vpp/compiler/pipeline.h"

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

void testPrimitivePipeline() {
    using namespace vietvm::compiler;
    using namespace vietvm::bytecode;
    const std::unordered_set<int> retiredOpCodes{
        OP_VM_KY_TU_UNICODE,
        OP_VM_MA_DIEM_UNICODE,
        OP_VM_SO_THUC_DAC_BIET,
        OP_VM_TASK_CANCEL,
        OP_VM_TASK_SCHEDULE,
        OP_VM_TASK_STATUS,
        OP_VM_TASK_WAIT,
    };
    // Each serialized VM opcode must have a descriptor or be explicitly retired.
    // An unregistered opcode would bypass intrinsic lowering and arity checking.
    for (int raw = OP_VM_BIEN_DICH_PHAN_TICH; raw <= OP_VM_FLOAT_FROM_BITS; ++raw) {
        if (retiredOpCodes.count(raw)) {
            expect(intrinsicByOpcode(raw) == nullptr && !isKnownOpcode(raw) &&
                       verifyBytecode({{raw, 0, 0, 0}}).has_value(),
                   "retired opcode is rejected: " + std::to_string(raw));
        } else {
            expect(intrinsicByOpcode(raw) != nullptr && isKnownOpcode(raw),
                   "VM opcode has intrinsic descriptor: " + std::to_string(raw));
        }
    }
    for (const auto *retired : {"ky_tu_unicode_vm", "ma_diem_unicode_vm",
                                "so_thuc_dac_biet_vm", "task_vm_schedule",
                                "task_vm_wait", "task_vm_cancel", "task_vm_status"}) {
        expect(intrinsicByName(retired) == nullptr,
               std::string("retired native name not callable: ") + retired);
    }
    std::unordered_set<int> opcodes;
    for (const auto &primitive : kVmIntrinsics) {
        const std::string name(primitive.name);
        expect(opcodes.insert(primitive.opcode).second, name + ": opcode duy nhất");
        expect(isKnownOpcode(primitive.opcode) &&
                   opcodeName(primitive.opcode).find("OP_VM_") == 0,
               name + ": opcode được đăng ký");
        expect(verifyBytecode({{primitive.opcode, 1, 0, 0}}).has_value(),
               name + ": verifier từ chối immediate không hợp lệ");
        std::string arguments;
        for (std::size_t i = 0; i < primitive.arity; ++i) {
            if (i != 0) arguments += ", ";
            arguments += "rỗng";
        }
        for (const std::string prefix : {"in ", "gọi "}) {
            CompilationContext context;
            auto artifacts = compilePipeline(context, prefix + name + "(" + arguments + ");",
                                             keywordMap, false);
            bool found = false;
            for (auto &value : artifacts.ir.values) {
                if (value.opcode != IrValueOpcode::Intrinsic) continue;
                found = value.intrinsicOpcode == primitive.opcode &&
                        value.operands.size() == primitive.arity + 1;
                // Mutating a validated IR must not silently emit a different primitive.
                const int saved = value.intrinsicOpcode;
                value.intrinsicOpcode = -1;
                expect(!analyzeDirectIrSupport(artifacts.ir).supported,
                       name + ": từ chối opcode IR hỏng");
                value.intrinsicOpcode = saved;
            }
            expect(found, name + ": parser/semantic hạ thành IR intrinsic");
            std::size_t emitted = 0;
            for (const auto &instruction : artifacts.bytecode) {
                emitted += instruction.op == primitive.opcode;
                expect(instruction.op != OP_GOI && instruction.op != OP_GOI_GIAN_TIEP,
                       name + ": không dispatch lời gọi theo tên");
            }
            expect(emitted == 1, name + ": phát đúng một primitive");
        }
        bool arityRejected = false;
        try {
            CompilationContext context;
            (void)compilePipeline(context, "in " + name + "(" + arguments +
                (arguments.empty() ? "rỗng" : ", rỗng") + ");", keywordMap, false);
        } catch (const std::exception &) {
            arityRejected = true;
        }
        expect(arityRejected, name + ": sai arity bị từ chối trước runtime");
    }

    CompilationContext context;
    const auto shadowed = compilePipeline(context,
        "hàm do_dai(x) { trả về 42; }; in do_dai([]);", keywordMap, false);
    for (const auto &value : shadowed.ir.values) {
        expect(value.opcode != IrValueOpcode::Intrinsic,
               "hàm thư viện/người dùng được ưu tiên hơn alias primitive không reserved");
    }
}

} // namespace

int main() {
    testMalformedAstReferencesBecomeUnsupportedIr();
    testInvalidIrIsRejectedBeforeEmission();
    testBytecodeVerifierRejectsMalformedMetadata();
    testPrimitivePipeline();

    if (failures != 0) {
        std::cerr << failures << " RC internal hardening check(s) failed\n";
        return 1;
    }
    std::cout << "RC internal hardening: PASS\n";
    return 0;
}
