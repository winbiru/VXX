// vm.cpp
#include "include/vm.h"
#include <iostream>
#include <stdexcept>

VM::VM(const std::vector<Instruction>& code) : code(code), pc(0) {}

void VM::run() {
    while (pc < code.size()) {
        const Instruction &instr = code[pc];
        switch (instr.op) {
            case OP_TAI_SO: {
                // Đẩy hằng số lên stack
                stack.push_back(instr.operand);
                break;
            }
            case OP_CONG: {
                // Cộng 2 giá trị trên stack
                if (stack.size() < 2) {
                    throw std::runtime_error("Lỗi: không đủ giá trị để cộng");
                }
                int b = stack.back(); stack.pop_back();
                int a = stack.back(); stack.pop_back();
                int sum = a + b;
                stack.push_back(sum);
                break;
            }
            case OP_IN: {
                if (stack.empty()) {
                    throw std::runtime_error("Lỗi: stack rỗng, không có gì để in");
                }
                int value = stack.back(); stack.pop_back();
                std::cout << value << std::endl;
                break;
            }
            case OP_DUNG: {
                return; // Dừng chương trình
            }
            default:
                throw std::runtime_error("Opcode không xác định");
        }
        pc++;
    }
}
