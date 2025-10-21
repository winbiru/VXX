// Compiler.h
#ifndef COMPILER_H
#define COMPILER_H

#include <string>
#include <vector>
#include <unordered_map>
#include "../instruction.h"

// Public API: chỉ cần compileSource ở header
// (mọi helper/chi tiết nội bộ để ở Compiler.cpp và là static/internal)
std::vector<Instruction> compileSource(
    const std::string &source,
    const std::unordered_map<std::string, Opcode> &keywordMap);
    // static std::unordered_map<std::string, CompileFunc> compileMap;

#endif // COMPILER_H
