// vm.h
#ifndef VM_H
#define VM_H

#include <vector>
#include <stack>
#include <unordered_map>
#include <string>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

#include "instruction.h"

class VM {
public:
    explicit VM(const std::vector<Instruction>& code);
    void run();
    VM(const std::vector<Instruction>& code, const std::vector<std::string>& pool);
private:
    std::vector<Instruction> bytecode;              // Mã bytecode
    std::vector<std::string> stringPool;
    using StackValue = std::variant<int, std::string>;
    std::vector<StackValue> stack;
    std::optional<StackValue> switchValue;
    bool skippingCase = false;
    bool inSwitchBlock = false;
    std::unordered_map<int, StackValue> variables;  // Biến tạm thời (nếu cần mở rộng)

    std::stack<size_t> loopStartStack;              // Stack hỗ trợ cho vòng lặp (for/while)
    std::stack<size_t> ifElseStack;                 // Stack hỗ trợ khối if/else
    std::stack<size_t> blockStack;                  // Stack theo dõi các khối {}

    size_t pc = 0;                                  // Program counter
    int instructionPointer = 0;
    bool running = true;                            // Trạng thái thực thi
    int vi_tri_dieu_kien = -1;
    struct SwitchFrame {
        size_t startPc;
        size_t endPc;           // 0 nếu chưa biết
        int blockDepthAtStart;
        std::optional<std::variant<int, std::string>> savedSwitchValue; // nếu cần giữ
    };
    std::vector<SwitchFrame> switchStack; // khởi tạo rỗng
    int blockDepth = 0;                   // tăng khi OP_MO_KHOI, giảm khi OP_DONG_KHOI

    // Các hàm phụ trợ
    void execute(const Instruction& inst);
    int pop();
    void push(int value);

};

#endif // VM_H
