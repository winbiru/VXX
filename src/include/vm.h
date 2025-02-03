// vm.h
#ifndef VM_H
#define VM_H

#include <vector>
#include "instruction.h"

class VM {
public:
    VM(const std::vector<Instruction>& code);
    void run();
private:
    std::vector<Instruction> code;
    std::vector<int> stack;
    size_t pc; // Program Counter
};

#endif // VM_H
