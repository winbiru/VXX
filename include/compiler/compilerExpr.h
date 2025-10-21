#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "instruction.h"
#include "name_op.h"

void compileExpr(const std::string &expr,
                 std::vector<Instruction> &bytecode,
                 std::unordered_map<std::string,int> &symTab,
                 int &nextId,
                 const std::unordered_map<std::string,Opcode> &keywordMap);