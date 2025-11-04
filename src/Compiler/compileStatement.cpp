// Replace or merge into your existing compileStatement implementation.
// Adds support for implicit function calls like: name(arg1, arg2);

#include "compiler/compileStatement.h"
#include "compiler/compilerExpr.h"
#include "common/Utility.h"
#include "common/storeString.h"
#include <stdexcept>
#include <sstream>
#include <vector>
#include <algorithm>
#include <iostream>

#include "common/Lex_utils.h"

// Forward-declare splitArgs if not available elsewhere (or reuse your repo's splitArgs)
static std::vector<std::string> splitArgsLocal(const std::string &s) {
    std::vector<std::string> res;
    std::string cur;
    int depth = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '(') { depth++; cur.push_back(c); }
        else if (c == ')') { depth--; cur.push_back(c); }
        else if (c == ',' && depth == 0) {
            res.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) res.push_back(cur);
    // trim spaces
    for (auto &t : res) {
        size_t a = t.find_first_not_of(" \t\n\r");
        size_t b = t.find_last_not_of(" \t\n\r");
        if (a == std::string::npos) t = "";
        else t = t.substr(a, b - a + 1);
    }
    return res;
}

void compileStatement(const std::vector<std::string>& tokens, size_t &pos,
                      std::vector<Instruction>& bytecode,
                      std::unordered_map<std::string,int>& symTab,
                      int &nextId,
                      const std::unordered_map<std::string,Opcode>& kwMap)
{
    if (pos >= tokens.size()) return;

    const std::string &tk = tokens[pos];

    // If token is a keyword handled via compileMap elsewhere, assume dispatcher handles it.
    // Here we implement implicit function call detection: identifier followed by '('
    if (vietvm::compiler::isVariable(tk) && (pos + 1) < tokens.size() && tokens[pos+1] == "(") {
        std::string fname = tk;
        ++pos; // move to '('
        auto pr = vietvm::compiler::extractParens(tokens, pos);
        std::string inside = pr.first;
        pos = pr.second; // position after ')'

        // split and compile args left-to-right
        std::vector<std::string> args = splitArgsLocal(inside);
        int compiledArgs = 0;
        for (const auto &aexpr : args) {
            if (aexpr.empty()) continue;
            compileExpr(aexpr, bytecode, symTab, nextId, kwMap);
            ++compiledArgs;
        }

        // resolve function id from symTab (set when compiling function) if possible
        int funcId = -1;
        auto it = symTab.find(fname);
        if (it != symTab.end()) funcId = it->second;

        if (funcId >= 0) {
            std::cerr << "[DBG] compileStatement: emitting OP_GOI fname=" << fname
          << " funcId=" << funcId << " argc=" << compiledArgs << " pos=" << pos << std::endl;

            bytecode.push_back({OP_GOI, funcId, compiledArgs, 0});
        } else {
          //   std::cerr << "[DBG] compileStatement: emitting OP_GOI fname=" << fname
          // << " funcId=" << funcId << " argc=" << compiledArgs << " pos=" << pos << std::endl;

            // fallback: emit using string pool index; VM will try to resolve nameIndex -> hamId
            int nameIndex = vietvm::compiler::StringPool::getOrInsertString(fname);
            bytecode.push_back({OP_GOI, nameIndex, compiledArgs, 0});
            // size_t idx = bytecode.size()-1;
            // std::cerr << "[DBG-BYTECODE] OP_GOI pushed into bytecode vector at idx=" << idx
            //           << " op=" << bytecode[idx].op
            //           << " operand=" << bytecode[idx].operand
            //           << " operandIndex=" << bytecode[idx].operandIndex << std::endl;
        }

        // consume optional semicolon
        if (pos < tokens.size() && tokens[pos] == ";") ++pos;
        return;
    }

    // Fallback: treat as expression or other statement
    // Use existing extractExpressionUntilSemicolon + compileExpr
    {
        auto pr = vietvm::compiler::extractExpressionUntilSemicolon(tokens, pos);
        if (!pr.first.empty()) {
            compileExpr(pr.first, bytecode, symTab, nextId, kwMap);
        }
        pos = pr.second;
    }
}