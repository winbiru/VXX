#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "vpp/core/message_constants.h"
#include "vpp/runtime/vm.h"

namespace {

int failures = 0;

class CoutCapture {
public:
    CoutCapture() : previous_(std::cout.rdbuf(captured_.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(previous_); }

    std::string str() const { return captured_.str(); }

private:
    std::ostringstream captured_;
    std::streambuf *previous_;
};

void expectEqual(const std::string &actual, const std::string &expected, const std::string &name) {
    if (actual != expected) {
        std::cerr << "FAIL: " << name << "\nexpected:\n" << expected
                  << "actual:\n" << actual;
        ++failures;
    }
}

std::string runAndCapture(const std::vector<Instruction> &code,
                          const std::vector<std::string> &stringPool = {}) {
    CoutCapture capture;
    VM vm(code, stringPool);
    vm.run();
    return capture.str();
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

    CoutCapture capture;
    VM vm(root, {});
    vm.hamBytecodeMap.emplace(7, function);
    vm.run();
    expectEqual(capture.str(), "[IN] 42\n",
                "function call binds an argument and returns a value");
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

} // namespace

int main() {
    try {
        testIntegerArithmeticAndModulo();
        testIntegerComparisons();
        testBranchOpcodes();
        testBranchBoundaryError();
        testFunctionCallParameterAndReturn();
        testStringPushAndPrint();
        testStackLiteralAndUnaryOpcodes();
        testVariableStackIncrementAndDecrement();
        testNativeAdapterCalls();
        testListLiteralAndPrint();
        testListIndexRead();
        testListIndexOutOfRange();
        testListIndexAssignment();
        testNestedListWireDecodeAndChainedRead();
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
