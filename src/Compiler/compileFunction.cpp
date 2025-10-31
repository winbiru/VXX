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
    // Expect token "Hàm"
    if (pos >= tokens.size() || tokens[pos] != "Hàm") return;
    pos++; // skip "Hàm"

    if (pos >= tokens.size()) return;
    std::string hamTen = tokens[pos]; // tên hàm
    pos++;

    // --- parse parameter list if present ---
    std::vector<std::string> params;
    if (pos < tokens.size() && tokens[pos] == "(") {
        pos++; // skip '('
        while (pos < tokens.size() && tokens[pos] != ")") {
            std::string p = tokens[pos];
            if (p == ",") { pos++; continue; }
            // trim whitespace (if tokenizer may deliver spaces)
            size_t a = p.find_first_not_of(" \t\n\r");
            size_t b = p.find_last_not_of(" \t\n\r");
            if (a != std::string::npos) {
                params.push_back(p.substr(a, b - a + 1));
            }
            pos++;
        }
        if (pos < tokens.size() && tokens[pos] == ")") pos++; // skip ')'
    }

    // Expect '{' next (after optional whitespace/tokens)
    if (pos >= tokens.size() || tokens[pos] != "{") return;
    pos++; // skip '{'

    // Gán ID cho hàm
    int hamId = nextId++;
    symTab[hamTen] = hamId;

    std::vector<Instruction> bytecode;

    // Biên dịch nội dung khối hàm
    bytecode.push_back({OP_MO_KHOI, 0, 0, 0}); // <-- initialize all 4 fields
    compileBlock(tokens, pos, bytecode, symTab, nextId, keywordMap);
    bytecode.push_back({OP_DONG_KHOI, 0, 0, 0}); // <-- initialize all 4 fields

    // Lưu vào bảng hàm
    hamBytecodeMap[hamId] = std::move(bytecode);
}