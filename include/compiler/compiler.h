// Compiler.h
#ifndef COMPILER_H
#define COMPILER_H

#include <string>
#include <vector>
#include <unordered_map>
#include "../vm/instruction.h"

// Public API: chỉ cần compileSource ở header
// (mọi helper/chi tiết nội bộ để ở Compiler.cpp và là static/internal)
// emitMainCall: nếu true (mặc định) thì sau khi compile sẽ tự động emit OP_GOI cho hàm main
std::vector<Instruction> compileSource(
    const std::string &source,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    bool emitMainCall = true);

#endif // COMPILER_H
