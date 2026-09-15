#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "vpp/bytecode/literal_wire.h"
#include "vpp/core/message_constants.h"
#include "vpp/runtime/vm.h"

namespace {

int failures = 0;

void expectEqual(const std::string &actual, const std::string &expected, const std::string &name) {
    if (actual != expected) {
        std::cerr << "FAIL: " << name << "\nexpected:\n" << expected
                  << "actual:\n" << actual;
        ++failures;
    }
}

void expectRuntimeError(const std::vector<Instruction> &code,
                        const std::string &expectedMessage,
                        const std::string &name,
                        const std::vector<std::string> &stringPool = {}) {
    try {
        VM vm(code, stringPool);
        vm.run();
        expectEqual("no error", expectedMessage, name);
    } catch (const std::exception &error) {
        const std::string message = error.what();
        if (message.find(expectedMessage) == std::string::npos) {
            expectEqual(message, expectedMessage, name);
        }
    }
}

std::string runAndCapture(const std::vector<Instruction> &code,
                          const std::vector<std::string> &stringPool = {}) {
    std::string output;
    VM vm(code, stringPool);
    vm.setOutputSink([&output](const std::string &text) { output += text; });
    vm.run();
    return output;
}

Instruction integer(int value) {
    return {OP_BIEN_SO, value, 0, 0};
}

Instruction opcode(Opcode value) {
    return {value, 0, 0, 0};
}

void testIntegerArithmeticAndModulo() {
    const std::vector<Instruction> code = {
        integer(7), integer(5), opcode(OP_CONG), opcode(OP_IN),
        integer(9), integer(4), opcode(OP_TRU), opcode(OP_IN),
        integer(6), integer(7), opcode(OP_NHAN), opcode(OP_IN),
        integer(20), integer(5), opcode(OP_CHIA), opcode(OP_IN),
        integer(20), integer(6), opcode(OP_MODULO), opcode(OP_IN),
        opcode(OP_DUNG_CHUONG_TRINH),
    };

    expectEqual(runAndCapture(code),
                "[IN] 12\n[IN] 5\n[IN] 42\n[IN] 4\n[IN] 2\n",
                "integer arithmetic and modulo");
}

void testIntegerComparisons() {
    const std::vector<Instruction> code = {
        integer(3), integer(3), opcode(OP_SO_SANH_BANG), opcode(OP_IN),
        integer(3), integer(4), opcode(OP_KHAC_BANG), opcode(OP_IN),
        integer(9), integer(4), opcode(OP_LON_HON), opcode(OP_IN),
        integer(2), integer(4), opcode(OP_NHO_HON), opcode(OP_IN),
        opcode(OP_DUNG_CHUONG_TRINH),
    };

    expectEqual(runAndCapture(code),
                "[IN] 1\n[IN] 1\n[IN] 1\n[IN] 1\n",
                "integer comparisons");
}

void testLogicAndComparisonBoundaryMatrix() {
    const std::vector<Instruction> code = {
        integer(1), integer(0), opcode(OP_Logic_VA), opcode(OP_IN),
        integer(1), integer(0), opcode(OP_Logic_HOAC), opcode(OP_IN),
        integer(4), integer(4), opcode(OP_LON_HON_HOAC_BANG), opcode(OP_IN),
        integer(4), integer(5), opcode(OP_NHO_HON_HOAC_BANG), opcode(OP_IN),
        opcode(OP_DUNG_CHUONG_TRINH),
    };

    expectEqual(runAndCapture(code),
                "[IN] 0\n[IN] 1\n[IN] 1\n[IN] 1\n",
                "logic and comparison boundary matrix");
}

void testBranchOpcodes() {
    const std::vector<Instruction> falseBranch = {
        integer(0), {OP_JUMP_IF_FALSE, 4, 0, 0},
        integer(99), opcode(OP_IN),
        integer(7), opcode(OP_IN),
        opcode(OP_DUNG_CHUONG_TRINH),
    };
    expectEqual(runAndCapture(falseBranch), "[IN] 7\n",
                "jump-if-false takes the false branch");

    const std::vector<Instruction> trueBranch = {
        integer(1), {OP_JUMP_IF_FALSE, 4, 0, 0},
        integer(8), opcode(OP_IN),
        opcode(OP_DUNG_CHUONG_TRINH),
    };
    expectEqual(runAndCapture(trueBranch), "[IN] 8\n",
                "jump-if-false falls through for a true condition");

    const std::vector<Instruction> unconditional = {
        {OP_JUMP, 3, 0, 0},
        integer(99), opcode(OP_IN),
        integer(5), opcode(OP_IN),
        opcode(OP_DUNG_CHUONG_TRINH),
    };
    expectEqual(runAndCapture(unconditional), "[IN] 5\n",
                "unconditional jump changes the program counter");
}

void testBranchBoundaryError() {
    const std::vector<Instruction> code = {
        {OP_JUMP, 99, 0, 0}, opcode(OP_DUNG_CHUONG_TRINH),
    };
    try {
        (void)runAndCapture(code);
        expectEqual("no error", std::string(vietvm::messages::kVmJumpAddressOutOfRange),
                    "jump rejects an out-of-range address");
    } catch (const std::exception &error) {
        const std::string message = error.what();
        if (message.find(vietvm::messages::kVmJumpAddressOutOfRange) == std::string::npos) {
            expectEqual(message, std::string(vietvm::messages::kVmJumpAddressOutOfRange),
                        "jump rejects an out-of-range address");
        }
    }
}

void testVerifierRejectsUnknownOpcodeBeforeDispatch() {
    expectRuntimeError(
        {{999, 0, 0, 0}},
        "bytecode không hợp lệ trong chương trình chính tại lệnh 0",
        "VM chạy bytecode verifier trước opcode dispatch");
}

void testOpcodeErrorMatrix() {
    expectRuntimeError(
        {integer(1), opcode(OP_CONG), opcode(OP_DUNG_CHUONG_TRINH)},
        std::string(vietvm::messages::kVmNotEnoughOperands),
        "binary arithmetic rejects a missing operand");

    expectRuntimeError(
        {{OP_JUMP_IF_FALSE, 1, 0, 0}, opcode(OP_DUNG_CHUONG_TRINH)},
        std::string(vietvm::messages::kVmJumpIfFalseEmptyStack),
        "conditional jump rejects an empty stack");

    expectRuntimeError(
        {{OP_CHUOI, 0, 0, 0}, {OP_JUMP_IF_FALSE, 2, 0, 0}, opcode(OP_DUNG_CHUONG_TRINH)},
        std::string(vietvm::messages::kVmJumpConditionMustBeInt),
        "conditional jump rejects a non-integer condition",
        {"không phải số"});

    expectRuntimeError(
        {{OP_GOI_GIAN_TIEP, 0, 0, 0}, opcode(OP_DUNG_CHUONG_TRINH)},
        std::string(vietvm::messages::kVmIndirectCallMissingReference),
        "indirect call requires a callee reference");

    expectRuntimeError(
        {{OP_BIEN_SO_FLOAT, 0, 0, 0}, opcode(OP_DUNG_CHUONG_TRINH)},
        vietvm::messages::formatMessage(
            vietvm::messages::kVmCannotConvertToFloat, {"not-a-number"}),
        "float literal reports invalid string-pool payload",
        {"not-a-number"});

    expectRuntimeError(
        {integer(4), integer(0), opcode(OP_CHIA), opcode(OP_DUNG_CHUONG_TRINH)},
        vietvm::messages::formatMessage(vietvm::messages::kVmDivisionByZero,
                                        {"4", "0"}),
        "division rejects zero divisor");

    expectRuntimeError(
        {integer(4), integer(0), opcode(OP_MODULO), opcode(OP_DUNG_CHUONG_TRINH)},
        std::string(vietvm::messages::kVmModuloByZero),
        "modulo rejects zero divisor");

    expectRuntimeError(
        {opcode(OP_CA), opcode(OP_DUNG_CHUONG_TRINH)},
        std::string(vietvm::messages::kVmCaseOutsideSwitch),
        "case rejects execution outside switch");
}

void testFunctionCallParameterAndReturn() {
    const std::vector<Instruction> root = {
        integer(41),
        {OP_GOI, 1, 7, 0},
        opcode(OP_IN),
        opcode(OP_DUNG_CHUONG_TRINH),
    };
    const std::vector<Instruction> function = {
        {OP_PARAM, 0, 0, 0},
        {OP_TEN_BIEN_GIA_TRI, 0, 0, 0},
        integer(1),
        opcode(OP_CONG),
        opcode(OP_TRA_VE),
    };

    std::string output;
    VM vm(root, {});
    vm.setOutputSink([&output](const std::string &text) { output += text; });
    vm.hamBytecodeMap.emplace(7, function);
    vm.run();
    expectEqual(output, "[IN] 42\n",
                "function call binds an argument and returns a value");
}

void testDefaultParameterBinding() {
    const std::vector<Instruction> root = {
        {OP_GOI, 0, 7, 0},
        opcode(OP_IN),
        opcode(OP_DUNG_CHUONG_TRINH),
    };
    const std::vector<Instruction> function = {
        {OP_KHOI_TAO, 0, 0, 0},
        {OP_PARAM_MAC_DINH, 0, 0, 0},
        {OP_TEN_BIEN_GIA_TRI, 0, 0, 0},
        opcode(OP_TRA_VE),
    };

    std::string output;
    VM vm(root, {"i:7"});
    vm.setOutputSink([&output](const std::string &text) { output += text; });
    vm.hamBytecodeMap.emplace(7, function);
    vm.run();
    expectEqual(output, "[IN] 7\n",
                "default parameter binds when an argument is omitted");
}

void testSwitchCaseAndDefault() {
    const std::vector<Instruction> matchingCase = {
        opcode(OP_MO_KHOI),
        integer(2), opcode(OP_CHON),
        {OP_CA, 1, -1, 0}, integer(10), opcode(OP_IN),
        {OP_CA, 2, -1, 0}, integer(20), opcode(OP_IN),
        opcode(OP_MAC_DINH), integer(30), opcode(OP_IN),
        opcode(OP_DONG_KHOI),
        opcode(OP_DUNG_CHUONG_TRINH),
    };
    expectEqual(runAndCapture(matchingCase), "[IN] 20\n",
                "switch selects a matching integer case");

    const std::vector<Instruction> defaultCase = {
        opcode(OP_MO_KHOI),
        integer(9), opcode(OP_CHON),
        {OP_CA, 1, -1, 0}, integer(10), opcode(OP_IN),
        opcode(OP_MAC_DINH), integer(30), opcode(OP_IN),
        opcode(OP_DONG_KHOI),
        opcode(OP_DUNG_CHUONG_TRINH),
    };
    expectEqual(runAndCapture(defaultCase), "[IN] 30\n",
                "switch falls through to default when no case matches");
}

void testThrowCatchAndUncaughtError() {
    const std::vector<Instruction> caught = {
        {OP_THU, 4, -1, 0},
        {OP_CHUOI, 0, 0, 0},
        opcode(OP_NEM),
        {OP_THU_KET_THUC, 7, 0, 0},
        {OP_BAT_LOI, 0, 0, 0},
        {OP_TEN_BIEN_GIA_TRI, 0, 0, 0},
        opcode(OP_IN),
        opcode(OP_DUNG_CHUONG_TRINH),
    };
    expectEqual(runAndCapture(caught, {"boom"}), "[IN] boom\n",
                "throw transfers control to catch and binds the error value");

    expectRuntimeError(
        {{OP_CHUOI, 0, 0, 0}, opcode(OP_NEM), opcode(OP_DUNG_CHUONG_TRINH)},
        vietvm::messages::formatMessage(vietvm::messages::kVmUncaughtException, {"boom"}),
        "uncaught throw propagates a runtime error",
        {"boom"});
}

void testStringPushAndPrint() {
    const std::vector<Instruction> code = {
        {OP_CHUOI, 0, 0, 0},
        opcode(OP_IN),
        opcode(OP_DUNG_CHUONG_TRINH),
    };

    expectEqual(runAndCapture(code, {"xin chào"}),
                "[IN] xin chào\n",
                "string push and print");
}

void testStackLiteralAndUnaryOpcodes() {
    const std::vector<Instruction> code = {
        opcode(OP_DUNG_GIA_TRI), opcode(OP_IN),
        opcode(OP_SAI_GIA_TRI), opcode(OP_IN),
        opcode(OP_RONG_GIA_TRI), opcode(OP_PHU_DINH), opcode(OP_IN),
        integer(0), opcode(OP_KHONG), opcode(OP_IN),
        integer(5), opcode(OP_PHU_DINH), opcode(OP_IN),
        opcode(OP_DUNG_CHUONG_TRINH),
    };

    expectEqual(runAndCapture(code),
                "[IN] 1\n[IN] 0\n[IN] 1\n[IN] 1\n[IN] 0\n",
                "stack literals and unary boolean operators");
}

void testVariableStackIncrementAndDecrement() {
    const std::vector<Instruction> code = {
        integer(10), {OP_TEN_BIEN_ID, 0, 0, 0}, opcode(OP_GAN),
        {OP_TEN_BIEN_GIA_TRI, 0, 0, 0}, opcode(OP_IN),
        {OP_TEN_BIEN_ID, 0, 0, 0}, opcode(OP_CONG_MOT), opcode(OP_IN),
        {OP_TEN_BIEN_GIA_TRI, 0, 0, 0}, opcode(OP_IN),
        {OP_TEN_BIEN_ID, 0, 0, 0}, opcode(OP_TRU_MOT), opcode(OP_IN),
        {OP_TEN_BIEN_GIA_TRI, 0, 0, 0}, opcode(OP_IN),
        opcode(OP_DUNG_CHUONG_TRINH),
    };

    expectEqual(runAndCapture(code),
                "[IN] 10\n[IN] 11\n[IN] 11\n[IN] 10\n[IN] 10\n",
                "variable stack assignment increment and decrement");
}

void testNativeAdapterCalls() {
    const std::vector<Instruction> direct = {
        {OP_CHUOI, 0, 2, 0},
        {OP_GOI, 1, -1, 0},
        opcode(OP_IN),
        opcode(OP_DUNG_CHUONG_TRINH),
    };
    expectEqual(runAndCapture(direct, {"do_dai", "chuoi_hoa", "abcd", "Abc"}),
                "[IN] 4\n",
                "direct call uses the native collection adapter");

    const std::vector<Instruction> indirect = {
        {OP_CHUOI, 0, 3, 0},
        {OP_CHUOI, 0, 1, 0},
        {OP_GOI_GIAN_TIEP, 1, 0, 0},
        opcode(OP_IN),
        opcode(OP_DUNG_CHUONG_TRINH),
    };
    expectEqual(runAndCapture(indirect, {"do_dai", "chuoi_hoa", "abcd", "Abc"}),
                "[IN] ABC\n",
                "indirect call uses the native text adapter");
}

void testListLiteralAndPrint() {
    const std::vector<Instruction> code = {
        {OP_LIST_LITERAL, 0, 0, 0}, opcode(OP_IN), opcode(OP_DUNG_CHUONG_TRINH),
    };
    // i=integer, s=string and n=null; fields use the same escaping protocol
    // as map literals, but list records have no key.
    expectEqual(runAndCapture(code, {"i\x1f" "1" "\x1e" "s\x1f" "xin" "\x1e" "n\x1f"}),
                "[IN] [1, xin, rỗng]\n",
                "list literal push and print");
}

void testListIndexRead() {
    const std::vector<Instruction> code = {
        {OP_LIST_LITERAL, 0, 0, 0}, integer(1), opcode(OP_DOC_CHI_SO), opcode(OP_IN),
        opcode(OP_DUNG_CHUONG_TRINH),
    };
    expectEqual(runAndCapture(code, {"i\x1f" "4" "\x1e" "s\x1f" "hai"}),
                "[IN] hai\n", "list index read");
}

void testListIndexOutOfRange() {
    const std::vector<Instruction> code = {
        {OP_LIST_LITERAL, 0, 0, 0}, integer(2), opcode(OP_DOC_CHI_SO),
        opcode(OP_DUNG_CHUONG_TRINH),
    };
    try {
        (void)runAndCapture(code, {"i\x1f" "4"});
        expectEqual("no error", std::string(vietvm::messages::kVmIndexOutOfRange),
                    "list index bounds error");
    } catch (const std::exception &error) {
        const std::string message = error.what();
        if (message.find(vietvm::messages::kVmIndexOutOfRange) == std::string::npos) {
            expectEqual(message, std::string(vietvm::messages::kVmIndexOutOfRange),
                        "list index bounds error");
        }
    }
}

void testListIndexAssignment() {
    const std::vector<Instruction> code = {
        {OP_LIST_LITERAL, 0, 0, 0}, {OP_TEN_BIEN_ID, 0, 0, 0}, opcode(OP_GAN),
        {OP_TEN_BIEN_GIA_TRI, 0, 0, 0}, integer(0), integer(9), opcode(OP_GAN_CHI_SO),
        {OP_TEN_BIEN_GIA_TRI, 0, 0, 0}, integer(0), opcode(OP_DOC_CHI_SO), opcode(OP_IN),
        opcode(OP_DUNG_CHUONG_TRINH),
    };
    expectEqual(runAndCapture(code, {"i\x1f" "1"}), "[IN] 9\n",
                "list index assignment");
}

void testNestedListWireDecodeAndChainedRead() {
    const std::vector<Instruction> code = {
        {OP_LIST_LITERAL, 0, 0, 0}, integer(0), opcode(OP_DOC_CHI_SO),
        integer(1), opcode(OP_DOC_CHI_SO), opcode(OP_IN),
        opcode(OP_DUNG_CHUONG_TRINH),
    };
    // Outer list fields contain an escaped inner list payload.  This guards
    // the common literal-wire helpers used by direct codegen and the VM.
    expectEqual(runAndCapture(code, {
                    std::string("l\x1f" "i\\f1\\ei\\f2") +
                    "\x1e" "l\x1f" "i\\f3"
                }),
                "[IN] 2\n",
                "nested list literal decode and chained index read");
}

void testNestedMapListWireDecode() {
    const std::vector<Instruction> code = {
        {OP_LIST_LITERAL, 0, 0, 0}, opcode(OP_IN),
        opcode(OP_DUNG_CHUONG_TRINH),
    };
    // [1, {"a": [2, 3]}]
    constexpr char rs = vietvm::bytecode::kLiteralRecordSeparator;
    constexpr char fs = vietvm::bytecode::kLiteralFieldSeparator;
    const std::string innerList =
        std::string("i") + fs + "2" + rs + "i" + fs + "3";
    const std::string innerMap =
        std::string("a") + fs + "l" + fs +
        vietvm::bytecode::escapeLiteralWireField(innerList);
    const std::string encoded =
        std::string("i") + fs + "1" + rs + "m" + fs +
        vietvm::bytecode::escapeLiteralWireField(innerMap);
    expectEqual(runAndCapture(code, {encoded}),
                "[IN] [1, {\"a\": [2, 3]}]\n",
                "nested map/list literal decode");
}

} // namespace

int main() {
    try {
        testIntegerArithmeticAndModulo();
        testIntegerComparisons();
        testLogicAndComparisonBoundaryMatrix();
        testBranchOpcodes();
        testBranchBoundaryError();
        testVerifierRejectsUnknownOpcodeBeforeDispatch();
        testOpcodeErrorMatrix();
        testFunctionCallParameterAndReturn();
        testDefaultParameterBinding();
        testSwitchCaseAndDefault();
        testThrowCatchAndUncaughtError();
        testStringPushAndPrint();
        testStackLiteralAndUnaryOpcodes();
        testVariableStackIncrementAndDecrement();
        testNativeAdapterCalls();
        testListLiteralAndPrint();
        testListIndexRead();
        testListIndexOutOfRange();
        testListIndexAssignment();
        testNestedListWireDecodeAndChainedRead();
        testNestedMapListWireDecode();
    } catch (const std::exception &error) {
        std::cerr << "FAIL: VM raised an exception: " << error.what() << '\n';
        return 1;
    }

    if (failures != 0) {
        std::cerr << failures << " VM opcode smoke unit test(s) failed\n";
        return 1;
    }

    std::cout << "VM opcode smoke unit tests passed\n";
    return 0;
}
