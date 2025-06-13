// compiler.h
#ifndef COMPILER_H
#define COMPILER_H

#include <vector>
#include <string>
#include <unordered_map>
#include "instruction.h"

// Hàm biên dịch: chuyển từ mã nguồn (string) sang vector<Instruction>
std::vector<Instruction> compileSource(const std::string &source);
extern std::unordered_map<std::string, int> symbolTable;
extern int nextSymbolIndex;
int getOrCreate(std::unordered_map<std::string, int>& table, const std::string& key, int& nextIndex);

#endif // COMPILER_H
