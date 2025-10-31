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
                                bytecode.push_back({OP_IN, 0, 0,0});
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
                                    bytecode.push_back({OP_KHOI_TAO, varId, 0,0});
                                }
                                compileLoop(tokens, pos, bytecode, symTab, nextId, kwMap);
                            }
                       };
    compileMap["chọn"] = compileSwitch;

    // Handler "hàm" (robust pos handling)
    compileMap["hàm"] = [](const std::vector<std::string>& tokens, size_t &pos,
                           std::vector<Instruction>& bytecode,
                           std::unordered_map<std::string,int>& symTab,
                           int& nextId,
                           const std::unordered_map<std::string,Opcode>& kwMap) {
        ++pos; // skip 'hàm'
        if (pos >= tokens.size()) throw std::runtime_error("compile: thiếu tên hàm sau 'hàm'");
        std::string fname = tokens[pos++];
        int nameIndex = vietvm::compiler::StringPool::storeString(fname);

        // assign function id
        int hamId = nextId++;
        symTab[fname] = hamId;

        // parse parameter list if present
        std::vector<std::string> params;
        if (pos < tokens.size() && tokens[pos] == "(") {
            auto pr = vietvm::compiler::extractParens(tokens, pos);
            // extractParens should return pair<inside, newPos>
            std::string inside = pr.first;
            pos = pr.second;
            // split args
            std::stringstream ss(inside);
            std::string item;
            while (std::getline(ss, item, ',')) {
                size_t a = item.find_first_not_of(" \t\n\r");
                size_t b = item.find_last_not_of(" \t\n\r");
                if (a != std::string::npos) params.push_back(item.substr(a, b - a + 1));
            }
        }

        // Now pos should be at the next token after params. Expect '{'
        if (pos >= tokens.size() || tokens[pos] != "{") {
            throw std::runtime_error(std::string("compileBlock: expected '{' at pos=") + std::to_string(pos)
                                     + ", found token='" + (pos < tokens.size() ? tokens[pos] : "EOF") + "'");
        }
        // consume '{'
        ++pos;

        // compile function body into temporary vector
        std::vector<Instruction> funcCode;
        funcCode.push_back({OP_MO_KHOI, 0, 0, 0});

        // emit OP_PARAM prologue
        for (size_t i = 0; i < params.size(); ++i) {
            const std::string &pname = params[i];
            if (pname.empty()) continue;
            if (symTab.find(pname) == symTab.end()) symTab[pname] = nextId++;
            int varId = symTab[pname];
            funcCode.push_back({OP_PARAM, 0, varId, (int)i});
        }

        // Let compileBlock consume until matching '}' — compileBlock must update pos to point after '}'
        compileBlock(tokens, pos, funcCode, symTab, nextId, kwMap);

        // ensure compileBlock left pos at token after '}', if not adjust as needed
        // function epilogue
        funcCode.push_back({OP_DONG_KHOI, 0, 0, 0});
        funcCode.push_back({OP_DONG_LENH, 0, 0, 0});

        // store function code
        hamBytecodeMap[hamId] = std::move(funcCode);

        // Optionally emit an OP_HAM marker into outer bytecode for discovery
        bytecode.push_back({OP_HAM, hamId, nameIndex, 0});
    };

    // ---- Handler: gọi hàm bằng từ khóa 'gọi' ----
    // cú pháp: gọi <tên>(arg1, arg2, ...)
    // ---- Handler: gọi hàm bằng từ khóa 'gọi' ----
    compileMap["gọi"] = [](const std::vector<std::string>& tokens, size_t &pos,
                           std::vector<Instruction>& bytecode,
                           std::unordered_map<std::string,int>& symTab,
                           int& nextId,
                           const std::unordered_map<std::string,Opcode>& kwMap) {
        ++pos;
        if (pos >= tokens.size()) throw std::runtime_error("gọi: thiếu tên hàm");
        std::string fname = tokens[pos++];

        // resolve function id from symTab (must have been set when compiling declaration)
        int hamId = -1;
        auto itSym = symTab.find(fname);
        if (itSym != symTab.end()) {
            hamId = itSym->second;
        } else {
            // fallback: store name in string pool and emit OP_GOI with nameIndex (VM fallback will try resolve)
            hamId = -1;
        }

        // parse args
        if (pos >= tokens.size() || tokens[pos] != "(") throw std::runtime_error("gọi: thiếu '(' sau tên hàm");
        auto pr = vietvm::compiler::extractParens(tokens, pos);
        std::string inside = pr.first;
        pos = pr.second;

        std::vector<std::string> args = splitArgs(inside);
        int compiledArgs = 0;
        for (const auto &aexpr : args) {
            if (aexpr.empty()) continue;
            compileExpr(aexpr, bytecode, symTab, nextId, kwMap);
            ++compiledArgs;
        }

        if (hamId >= 0) {
            // emit with hamId in operand and argc in operandIndex
            bytecode.push_back({OP_GOI, hamId, compiledArgs, 0});
        } else {
            // fallback: emit with nameIndex so VM fallback can resolve (less ideal)
            int nameIndex = vietvm::compiler::StringPool::storeString(fname);
            bytecode.push_back({OP_GOI, nameIndex, compiledArgs, 0});
        }

        if (pos < tokens.size() && tokens[pos] == ";") ++pos;
    };
}