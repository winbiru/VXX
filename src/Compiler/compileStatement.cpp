//
// Created by nx_thang on 10/21/2025.
//

#include "../../include/compiler/compileStatement.h"

#include <string>
#include <unordered_map>
#include <vector>

#include "instruction.h"
#include "common/Utility.h"
#include "compiler/compileBlock.h"
#include "compiler/CompileRegistry.h"
#include "compiler/compilerExpr.h"

void compileStatement(const std::vector<std::string>& tokens, size_t &pos,
                             std::vector<Instruction> &bytecode,
                             std::unordered_map<std::string,int> &symTab,
                             int &nextId,
                             const std::unordered_map<std::string,Opcode> &keywordMap)
{
    if (pos >= tokens.size()) return;
    const std::string &tk = tokens[pos];

    // ---- Trường hợp Block ----
    if (tk == "{") {
        bytecode.push_back({OP_MO_KHOI,0,0});
        compileBlock(tokens, pos, bytecode, symTab, nextId, keywordMap);
        bytecode.push_back({OP_DONG_KHOI,0,0});
        return;
    }

    // ---- Trường hợp Keyword có CompileFunc riêng ----
    auto it = compileMap.find(tk);
    if (it != compileMap.end()) {
        it->second(tokens, pos, bytecode, symTab, nextId, keywordMap);
        return;
    }

    // ---- Trường hợp Keyword chưa có compileFunc (nhưng vẫn là opcode hợp lệ) ----
    if (keywordMap.count(tk)) {
        Opcode code = keywordMap.at(tk);
        bytecode.push_back({code,0,0});
        ++pos;
        return;
    }

    // ---- Còn lại là Biểu thức thông thường ----
    auto pr = vietvm::compiler::extractExpressionUntilSemicolon(tokens, pos);
    compileExpr(pr.first, bytecode, symTab, nextId, keywordMap);
    bytecode.push_back({OP_DONG_LENH,0,0});
    pos = pr.second;
}
