#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

} // namespace

int main() {
    try {
        testIntegerArithmeticAndModulo();
        testIntegerComparisons();
        testStringPushAndPrint();
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
