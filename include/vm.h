// vm.h
#ifndef VM_H
#define VM_H

#include <vector>
#include <stack>
#include <unordered_map>
#include <string>
#include <cstddef>
#include <cstdint>
#include "instruction.h"

class VM {
public:
    explicit VM(const std::vector<Instruction>& code);
    void run();
private:
    std::vector<Instruction> bytecode;       // Mã bytecode
    std::vector<int> stack;              // Stack chính để thực thi
    std::unordered_map<int, int> variables; // Biến tạm thời (nếu cần mở rộng)

    std::stack<size_t> loopStartStack;   // Stack hỗ trợ cho vòng lặp (for/while)
    std::stack<size_t> ifElseStack;      // Stack hỗ trợ khối if/else
    std::stack<size_t> blockStack;       // Stack theo dõi các khối {}

    size_t pc = 0;                       // Program counter
    int instructionPointer = 0;
    bool running = true;                 // Trạng thái thực thi

    // Các hàm phụ trợ
    void execute(const Instruction& inst);
    int pop();
    void push(int value);

};

#endif // VM_H
