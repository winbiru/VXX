//
// Created by nx_thang on 10/21/2025.
//

#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include "../vm/instruction.h"

void compileSwitch(const std::vector<std::string>& tokens, size_t &pos,
                   std::vector<Instruction>& bytecode,
                   std::unordered_map<std::string,int>& symTab,
                   int& nextId,
                   const std::unordered_map<std::string,Opcode>& keywordMap);
