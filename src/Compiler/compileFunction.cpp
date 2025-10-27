//
// Created by nx_thang on 10/27/2025.
//

#include "compiler/compileFunction.h"
#include "compiler/compileBlock.h"
#include "instruction.h"

void compileFunction(const std::vector<std::string>& tokens, size_t& pos,
                     std::unordered_map<std::string, int>& symTab,
                     int& nextId,
                     const std::unordered_map<std::string, Opcode>& keywordMap,
                     std::unordered_map<int, std::vector<Instruction>>& hamBytecodeMap)
{
    // tokens[pos] is "Hàm"
    if (tokens[pos] != "Hàm") return;
    pos++; // skip "Hàm"

    if (pos >= tokens.size()) return;
    std::string hamTen = tokens[pos]; // tên hàm
    pos++;

    if (pos >= tokens.size() || tokens[pos] != "{") return;
    pos++; // skip dấu mở khối

    // Gán ID cho hàm
    int hamId = nextId++;
    symTab[hamTen] = hamId;

    std::vector<Instruction> bytecode;

    // Biên dịch nội dung khối hàm
    bytecode.push_back({OP_MO_KHOI, 0, 0});
    compileBlock(tokens, pos, bytecode, symTab, nextId, keywordMap);
    bytecode.push_back({OP_DONG_KHOI, 0, 0});

    // Lưu vào bảng hàm
    hamBytecodeMap[hamId] = std::move(bytecode);
}
