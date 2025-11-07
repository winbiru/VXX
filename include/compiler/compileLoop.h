//
// Created by nx_thang on 10/21/2025.
//
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "../vm/instruction.h"
struct LoopIndices { int cond_index; int exit_jump_index; };

LoopIndices compileLoop(const std::vector<std::string>& tokens, size_t &pos,
                               std::vector<Instruction> &bytecode,
                               std::unordered_map<std::string,int> &symTab,
                               int &nextId,
                               const std::unordered_map<std::string,Opcode> &keywordMap);