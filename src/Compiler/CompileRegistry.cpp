//
// Created by nx_thang on 10/21/2025.
//

#include "../../include/compiler/CompileRegistry.h"

#include "common/LoopUltil.h"
#include "common/Utility.h"
#include "compiler/compileCondition.h"
#include "compiler/compileLoop.h"
#include "compiler/compilerExpr.h"
#include "compiler/compileSwitch.h"
#include "common/storeString.h"
#include "common/SymbolTable.h"
#include <sstream>

#include "compiler/compileBlock.h"
#include "compiler/compileStatement.h"

std::unordered_map<std::string, CompileFunc> compileMap;

static std::vector<std::string> splitArgs(const std::string &s) {
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
        // simple trim
        size_t a = t.find_first_not_of(" \t\n\r");
        size_t b = t.find_last_not_of(" \t\n\r");
        if (a == std::string::npos) t = "";
        else t = t.substr(a, b - a + 1);
    }
    return res;
}

void initCompileMap() {
    compileMap["in"]  = [](const std::vector<std::string>& tokens, size_t &pos,
                           std::vector<Instruction>& bytecode,
                           std::unordered_map<std::string,int>& symTab,
                           int& nextId,
                           const std::unordered_map<std::string,Opcode>& kwMap) {
                                ++pos; // bỏ qua "in"
                                auto pr = vietvm::compiler::extractExpressionUntilSemicolon(tokens, pos);
                                compileExpr(pr.first, bytecode, symTab, nextId, kwMap);
                                bytecode.push_back({OP_IN, 0, 0});
                                pos = pr.second;
    };

    compileMap["nếu"] = compileCondition;
    compileMap["lặp"] = [](const std::vector<std::string>& tokens, size_t &pos,
                       std::vector<Instruction>& bytecode,
                       std::unordered_map<std::string,int>& symTab,
                       int& nextId,
                       const std::unordered_map<std::string,Opcode>& kwMap) {
                            if (tokens[pos] == "lặp") {
                                std::string loopHeader = vietvm::compiler::extractParens(tokens, pos + 1).first;
                                std::vector<std::string> parts = splitLoopParts(loopHeader);
                                std::string varName = vietvm::compiler::extractAssignedVar(parts[0]);
                                if (!varName.empty()) {
                                    if (symTab.find(varName) == symTab.end()) {
                                        symTab[varName] = nextId++;
                                    }
                                    int varId = symTab[varName];
                                    bytecode.push_back({OP_KHOI_TAO, varId, 0});
                                }
                                compileLoop(tokens, pos, bytecode, symTab, nextId, kwMap);
                            }
                       };
    compileMap["chọn"] = compileSwitch;

    // ---- Handler: khai báo hàm ----
    compileMap["hàm"] = [](const std::vector<std::string>& tokens, size_t &pos,
                           std::vector<Instruction>& bytecode,
                           std::unordered_map<std::string,int>& symTab,
                           int& nextId,
                           const std::unordered_map<std::string,Opcode>& kwMap) {
        // tokens[pos] == "hàm"
        ++pos;
        if (pos >= tokens.size()) throw std::runtime_error("compile: thiếu tên hàm sau 'hàm'");
        std::string fname = tokens[pos];
        // lưu tên hàm vào string pool
        int nameIndex = vietvm::compiler::StringPool::storeString(fname);
        // emit OP_HAM với operandIndex = nameIndex
        bytecode.push_back({OP_HAM, 0, nameIndex});
        ++pos;

        // parse params if any: expect '(' ... ')'
        std::vector<std::string> params;
        if (pos < tokens.size() && tokens[pos] == "(") {
            auto pr = vietvm::compiler::extractParens(tokens, pos);
            std::string inside = pr.first;
            pos = pr.second;
            // split by comma
            std::stringstream ss(inside);
            std::string item;
            while (std::getline(ss, item, ',')) {
                // trim
                size_t a = item.find_first_not_of(" \t\n\r");
                size_t b = item.find_last_not_of(" \t\n\r");
                if (a != std::string::npos) {
                    params.push_back(item.substr(a, b - a + 1));
                }
            }
            // register params in symTab (assign ids) - these ids will be used by compiler to refer to param names if accessed
            for (auto &pname : params) {
                if (!pname.empty() && symTab.find(pname) == symTab.end()) {
                    symTab[pname] = nextId++;
                }
                // emit init for param variable? Optional: Expect compiler to read params from stack by varId convention.
            }
        }

        // Now expect body: either single statement or block
        if (pos < tokens.size() && tokens[pos] == "{") {
            // Emit block markers and compile body
            bytecode.push_back({OP_MO_KHOI, 0, 0});
            compileBlock(tokens, pos, bytecode, symTab, nextId, kwMap);
            bytecode.push_back({OP_DONG_KHOI, 0, 0});
        } else {
            // single statement as body
            compileStatement(tokens, pos, bytecode, symTab, nextId, kwMap);
        }
        // end function: optional OP_DONG_LENH to terminate last expression
        bytecode.push_back({OP_DONG_LENH, 0, 0});
    };

    // ---- Handler: gọi hàm bằng từ khóa 'gọi' ----
    // cú pháp: gọi <tên>(arg1, arg2, ...)
    compileMap["gọi"] = [](const std::vector<std::string>& tokens, size_t &pos,
                           std::vector<Instruction>& bytecode,
                           std::unordered_map<std::string,int>& symTab,
                           int& nextId,
                           const std::unordered_map<std::string,Opcode>& kwMap) {
        ++pos; // skip 'gọi'
        if (pos >= tokens.size()) throw std::runtime_error("gọi: thiếu tên hàm");
        std::string fname = tokens[pos++];
        int nameIndex = vietvm::compiler::StringPool::storeString(fname);

        // expect parens
        if (pos >= tokens.size() || tokens[pos] != "(") {
            throw std::runtime_error("gọi: thiếu '(' sau tên hàm");
        }
        auto pr = vietvm::compiler::extractParens(tokens, pos);
        std::string inside = pr.first;
        pos = pr.second;

        // split args and compile each arg expression (left-to-right)
        std::vector<std::string> args = splitArgs(inside);
        for (const auto &aexpr : args) {
            if (aexpr.empty()) continue;
            compileExpr(aexpr, bytecode, symTab, nextId, kwMap);
        }
        int argc = 0;
        for (auto &a : args) if (!a.empty()) ++argc;
        bytecode.push_back({OP_GOI, argc, nameIndex});
        // add statement terminator if next token is ';'
        if (pos < tokens.size() && tokens[pos] == ";") ++pos;
    };
}