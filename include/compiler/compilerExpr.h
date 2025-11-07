#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "../vm/instruction.h"
#include "../frontend/keywords.h"

void compileExpr(const std::string &expr,
                 std::vector<Instruction> &bytecode,
                 std::unordered_map<std::string,int> &symTab,
                 int &nextId,
                 const std::unordered_map<std::string,Opcode> &keywordMap);