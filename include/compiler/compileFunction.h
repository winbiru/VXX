//
// Created by nx_thang on 10/27/2025.
//

#ifndef COMPILEFUNCTION_H
#define COMPILEFUNCTION_H
#include <string>
#include <unordered_map>
#include <vector>

#include "instruction.h"

void compileFunction(const std::vector<std::string>& tokens, size_t& pos,
                     std::unordered_map<std::string, int>& symTab,
                     int& nextId,
                     const std::unordered_map<std::string, Opcode>& keywordMap,
                     std::unordered_map<int, std::vector<Instruction>>& hamBytecodeMap);



#endif //COMPILEFUNCTION_H
