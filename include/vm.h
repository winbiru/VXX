// vm.h
#ifndef VM_H
#define VM_H

#include <vector>
#include <stack>
#include <unordered_map>
#include <string>
#include <cstddef>
#include <cstdint>
#include <variant>

#include "instruction.h"

class VM {
public:
    explicit VM(const std::vector<Instruction>& code);
    void run();
    void loadStringPool(const std::vector<std::string>& pool); // ← THÊM DÒNG NÀY
private:
    std::vector<Instruction> bytecode;              // Mã bytecode
    using StackValue = std::variant<int, std::string>;
    std::vector<StackValue> stack;
    std::unordered_map<int, StackValue> variables;  // Biến tạm thời (nếu cần mở rộng)

    std::stack<size_t> loopStartStack;              // Stack hỗ trợ cho vòng lặp (for/while)
    std::stack<size_t> ifElseStack;                 // Stack hỗ trợ khối if/else
    std::stack<size_t> blockStack;                  // Stack theo dõi các khối {}

    size_t pc = 0;                                  // Program counter
    int instructionPointer = 0;
    bool running = true;                            // Trạng thái thực thi
    int vi_tri_dieu_kien = -1;
    std::vector<std::string> stringPool;            // Bộ nhớ lưu chuỗi hằng
    // Các hàm phụ trợ
    void execute(const Instruction& inst);
    int pop();
    void push(int value);

};

#endif // VM_H
