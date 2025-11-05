//
// Created by nx_thang on 10/21/2025.
//

#include "../../include/compiler/compileStatement.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <common/Utility.h>
#include <compiler/compileBlock.h>
#include <compiler/CompileRegistry.h>
#include <compiler/compilerExpr.h>
#include "common/storeString.h"
#include <sstream>
#include <iostream>

// Helper to split arguments string into individual argument expressions (handles nested parens)
static std::vector<std::string> splitArgsString(const std::string &s) {
    std::vector<std::string> res;
    std::string cur;
    int depth = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '(') { depth++; cur.push_back(c); }
        else if (c == ')') { depth--; cur.push_back(c); }
        else if (c == ',' && depth == 0) {
            // push trimmed current
            size_t a = cur.find_first_not_of(" \t\n\r");
            size_t b = cur.find_last_not_of(" \t\n\r");
            if (a == std::string::npos) res.push_back("");
            else res.push_back(cur.substr(a, b - a + 1));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        size_t a = cur.find_first_not_of(" \t\n\r");
        size_t b = cur.find_last_not_of(" \t\n\r");
        if (a == std::string::npos) res.push_back("");
        else res.push_back(cur.substr(a, b - a + 1));
    }
    return res;
}

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
        bytecode.push_back({OP_MO_KHOI,0,0,0});
        compileBlock(tokens, pos, bytecode, symTab, nextId, keywordMap);
        bytecode.push_back({OP_DONG_KHOI,0,0,0});
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
        bytecode.push_back({code,0,0,0});
        ++pos;
        return;
    }

    // ---- Trường hợp gọi hàm dạng identifier(args); ----
    if ((pos + 1) < tokens.size() && tokens[pos+1] == "(") {
        std::string ident = tokens[pos];

        // extract content inside parens; extractParens expects the position of '('
        auto pr = vietvm::compiler::extractParens(tokens, pos + 1);
        std::string inside = pr.first;
        pos = pr.second; // pos now points after ')'

        // split args and compile each expression
        std::vector<std::string> args = splitArgsString(inside);
        int compiledArgs = 0;
        for (const auto &aexpr : args) {
            if (aexpr.empty()) continue;
            compileExpr(aexpr, bytecode, symTab, nextId, keywordMap);
            ++compiledArgs;
        }

        // resolve function id if declared, otherwise emit nameIndex fallback
        int hamId = -1;
        auto itSym = symTab.find(ident);
        if (itSym != symTab.end()) hamId = itSym->second;

        if (hamId >= 0) {
            bytecode.push_back({OP_GOI, compiledArgs, hamId, 0});
        } else {
            int nameIndex = vietvm::compiler::StringPool::storeString(ident);
            bytecode.push_back({OP_GOI, compiledArgs, nameIndex, 0});
        }

        // optional semicolon
        if (pos < tokens.size() && tokens[pos] == ";") ++pos;
        return;
    }

    // ---- Còn lại là Biểu thức thông thường ----
    auto pr = vietvm::compiler::extractExpressionUntilSemicolon(tokens, pos);
    compileExpr(pr.first, bytecode, symTab, nextId, keywordMap);
    bytecode.push_back({OP_DONG_LENH,0,0,0});
    pos = pr.second;
}