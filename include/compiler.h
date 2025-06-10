// compiler.h
#ifndef COMPILER_H
#define COMPILER_H

#include <vector>
#include <string>
#include "instruction.h"

// Hàm biên dịch: chuyển từ mã nguồn (string) sang vector<Instruction>
std::vector<Instruction> compileSource(const std::string &source);

#endif // COMPILER_H
