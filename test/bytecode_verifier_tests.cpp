#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "vpp/bytecode/verifier.h"

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

vietvm::bytecode::BytecodeVerificationContext context(
    std::size_t stringPoolSize,
    std::unordered_set<int> functionIds = {}) {
    vietvm::bytecode::BytecodeVerificationContext result;
    result.stringPoolSize = stringPoolSize;
    result.functionIds = std::move(functionIds);
    return result;
}

void testAcceptsValidProgram() {
    const std::vector<Instruction> code = {
        {OP_HAM, 0, 7, 0},
        {OP_CHUOI, 0, 1, 0},
        {OP_JUMP, 3, 0, 0},
        {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
    };
    const auto result = vietvm::bytecode::verifyBytecode(
        code, context(2, {7}));
    expect(!result.has_value(), "verifier chấp nhận bytecode hợp lệ");
}

void testRejectsUnknownOpcode() {
    const std::vector<Instruction> code = {
        {999, 0, 0, 0},
    };
    const auto result = vietvm::bytecode::verifyBytecode(code, context(0));
    expect(result.has_value() && result->instructionIndex == 0,
           "verifier từ chối opcode ngoài tập lệnh");
}

void testRejectsInvalidJumpAndTryTargets() {
    const auto badJump = vietvm::bytecode::verifyBytecode(
        {{OP_JUMP, 4, 0, 0}, {OP_DUNG_CHUONG_TRINH, 0, 0, 0}},
        context(0));
    expect(badJump.has_value(), "verifier từ chối địa chỉ nhảy ngoài phạm vi");

    const auto badTry = vietvm::bytecode::verifyBytecode(
        {{OP_THU, 1, -1, 0}, {OP_DUNG_CHUONG_TRINH, 0, 0, 0}},
        context(0));
    expect(badTry.has_value(), "verifier yêu cầu OP_THU trỏ tới OP_BAT_LOI");
}

void testRejectsInvalidPoolReferences() {
    const auto badString = vietvm::bytecode::verifyBytecode(
        {{OP_CHUOI, 0, 2, 0}}, context(2));
    expect(badString.has_value(), "verifier từ chối literal ngoài StringPool");

    const auto badNativeName = vietvm::bytecode::verifyBytecode(
        {{OP_GOI, 0, -3, 0}}, context(2));
    expect(badNativeName.has_value(),
           "verifier từ chối tên hàm mã hóa ngoài StringPool");
}

void testRejectsInvalidFunctionMetadata() {
    const auto badFunction = vietvm::bytecode::verifyBytecode(
        {{OP_HAM, 0, 9, 0}}, context(1, {7}));
    expect(badFunction.has_value(),
           "verifier từ chối function id không có bytecode");

    const auto badClosure = vietvm::bytecode::verifyBytecode(
        {{OP_TAO_DONG_BAO, 9, 1, 0}}, context(0, {7}));
    expect(badClosure.has_value(),
           "verifier từ chối closure trỏ tới function id không tồn tại");
}

} // namespace

int main() {
    testAcceptsValidProgram();
    testRejectsUnknownOpcode();
    testRejectsInvalidJumpAndTryTargets();
    testRejectsInvalidPoolReferences();
    testRejectsInvalidFunctionMetadata();

    if (failures != 0) {
        std::cerr << failures << " kiểm tra bytecode verifier thất bại\n";
        return 1;
    }
    std::cout << "bytecode verifier: đạt\n";
    return 0;
}
