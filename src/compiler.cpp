// compiler.cpp
#include "../include/compiler.h"
#include "../include/common/Utility.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <cctype>
#include <iostream>
#include <unordered_map>
#include "../include/instruction.h"
#include "common/LoopUltil.h" // nếu bạn đã có splitLoopParts(...) ở đây
#include <functional>
#include <string>

#include "common/Expression.h"
#include "common/Lex_utils.h"
#include "common/SymbolTable.h"

std::vector<std::string> stringPool;
int storeString(const std::string& s) {
    stringPool.push_back(s);
    return static_cast<int>(stringPool.size() - 1);
}


// ---------- compileExpr: produce bytecode for an expression or assignment ----------
static void compileExpr(const std::string &expr,
                        std::vector<Instruction> &bytecode,
                        std::unordered_map<std::string,int> &symTab,
                        int &nextId,
                        const std::unordered_map<std::string,Opcode> &keywordMap)
{
    if (vietvm::compiler::trim(expr).empty()) return;

    auto toks = vietvm::compiler::tokenize(expr);
    auto itEq = std::find(toks.begin(), toks.end(), "=");

    if (itEq != toks.end() && std::distance(toks.begin(), itEq) == 1) {
        std::string varName = toks[0];
        int dstId = vietvm::compiler::SymbolTable::getOrCreate(symTab, varName, nextId);
        // ensureInitialized(dstId);

        std::vector<std::string> rhsTokens(itEq + 1, toks.end());
        auto postfix = vietvm::compiler::convertToPostfix(rhsTokens);

        for (const auto &tk : postfix) {
            if (vietvm::compiler::isNumber(tk)) {
                bytecode.push_back({OP_BIEN_SO, std::stoi(tk), 0});
            } else if (vietvm::compiler::isStringLiteral(tk)) {
                int strIndex = storeString(tk.substr(1, tk.size() - 2));
                bytecode.push_back({OP_CHUOI, 0, strIndex});
            } else if (vietvm::compiler::isVariable(tk)) {
                int id = vietvm::compiler::SymbolTable::getOrCreate(symTab, tk, nextId);
                // ensureInitialized(id);
                bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, id});
            } else {
                if (tk == "+") bytecode.push_back({OP_CONG,0,0});
                else if (tk == "-") bytecode.push_back({OP_TRU,0,0});
                else if (tk == "*") bytecode.push_back({OP_NHAN,0,0});
                else if (tk == "/") bytecode.push_back({OP_CHIA,0,0});
                else if (tk == "%") bytecode.push_back({OP_MODULO,0,0});
                else if (tk == "==") bytecode.push_back({OP_SO_SANH_BANG,0,0});
                else if (tk == "!") bytecode.push_back({OP_PHU_DINH,0,0});
                else if (tk == "!=") bytecode.push_back({OP_KHAC_BANG,0,0});
                else if (tk == "<") bytecode.push_back({OP_NHO_HON,0,0});
                else if (tk == ">") bytecode.push_back({OP_LON_HON,0,0});
                else if (tk == "<=") bytecode.push_back({OP_NHO_HON_HOAC_BANG,0,0});
                else if (tk == ">=") bytecode.push_back({OP_LON_HON_HOAC_BANG,0,0});
                else if (tk == "&&") bytecode.push_back({OP_Logic_VA,0,0});
                else if (tk == "||") bytecode.push_back({OP_Logic_HOAC,0,0});
                else throw std::runtime_error("compileExpr: unsupported operator " + tk);
            }
        }

        bytecode.push_back({OP_TEN_BIEN_ID, 0, dstId});
        bytecode.push_back({OP_GAN,0,0});
        return;
    }

    auto postfix = vietvm::compiler::convertToPostfix(toks);
    for (const auto &tk : postfix) {
        if (vietvm::compiler::isNumber(tk)) {
            bytecode.push_back({OP_BIEN_SO, std::stoi(tk), 0});
        } else if (vietvm::compiler::isStringLiteral(tk)) {
            int strIndex = storeString(tk.substr(1, tk.size() - 2));
            bytecode.push_back({OP_CHUOI, 0, strIndex});
        } else if (vietvm::compiler::isVariable(tk)) {
            int id = vietvm::compiler::SymbolTable::getOrCreate(symTab, tk, nextId);
            // ensureInitialized(id);
            bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, id});
        } else {
            if (tk == "+") bytecode.push_back({OP_CONG,0,0});
            else if (tk == "-") bytecode.push_back({OP_TRU,0,0});
            else if (tk == "*") bytecode.push_back({OP_NHAN,0,0});
            else if (tk == "/") bytecode.push_back({OP_CHIA,0,0});
            else if (tk == "%") bytecode.push_back({OP_MODULO,0,0});
            else if (tk == "!") bytecode.push_back({OP_PHU_DINH,0,0});
            else if (tk == "==") bytecode.push_back({OP_SO_SANH_BANG,0,0});
            else if (tk == "!=") bytecode.push_back({OP_KHAC_BANG,0,0});
            else if (tk == "<") bytecode.push_back({OP_NHO_HON,0,0});
            else if (tk == ">") bytecode.push_back({OP_LON_HON,0,0});
            else if (tk == "<=") bytecode.push_back({OP_NHO_HON_HOAC_BANG,0,0});
            else if (tk == ">=") bytecode.push_back({OP_LON_HON_HOAC_BANG,0,0});
            else if (tk == "&&") bytecode.push_back({OP_Logic_VA,0,0});
            else if (tk == "||") bytecode.push_back({OP_Logic_HOAC,0,0});
            else throw std::runtime_error("compileExpr: unsupported token " + tk);
        }
    }
}

// forward declarations
static void compileStatement(const std::vector<std::string>& tokens, size_t &pos,
                             std::vector<Instruction> &bytecode,
                             std::unordered_map<std::string,int> &symTab,
                             int &nextId,
                             const std::unordered_map<std::string,Opcode> &keywordMap);

static void compileBlock(const std::vector<std::string>& tokens, size_t &pos,
                         std::vector<Instruction> &bytecode,
                         std::unordered_map<std::string,int> &symTab,
                         int &nextId,
                         const std::unordered_map<std::string,Opcode> &keywordMap);

// ---------- compileConditionBlock (if) ----------
static void compileConditionBlock(const std::vector<std::string>& tokens, size_t &pos,
                                  std::vector<Instruction> &bytecode,
                                  std::unordered_map<std::string,int> &symTab,
                                  int &nextId,
                                  const std::unordered_map<std::string,Opcode> &keywordMap)
{
    // tokens[pos] is 'nếu' (OP_NEU)
    // parse parens
    auto parenPair = vietvm::compiler::extractParens(tokens, pos + 1); // expects '(' at pos+1
    std::string condExpr = parenPair.first;
    size_t afterParen = parenPair.second;

    // compile condition: push OP_NEU/OP_MO_NGOAC markers similar to earlier design
    bytecode.push_back({OP_NEU,0,0});
    bytecode.push_back({OP_MO_NGOAC,0,0});
    // compile condition expression
    compileExpr(condExpr, bytecode, symTab, nextId, keywordMap);
    // push jump if false placeholder
    bytecode.push_back({OP_JUMP_IF_FALSE, 0, 0});
    int jumpIndex = (int)bytecode.size() - 1;
    bytecode.push_back({OP_DONG_NGOAC,0,0});

    pos = afterParen; // pos after ')'

    // Now expect block
    if (pos < tokens.size() && tokens[pos] == "{") {
        // open block
        bytecode.push_back({OP_MO_KHOI,0,0});
        compileBlock(tokens, pos, bytecode, symTab, nextId, keywordMap);
        bytecode.push_back({OP_DONG_KHOI,0,0});
    } else {
        // single statement after if
        compileStatement(tokens, pos, bytecode, symTab, nextId, keywordMap);
    }

    // backpatch jump target to after the if body
    int target = (int)bytecode.size();
    bytecode[jumpIndex].operand = target;
}

// ---------- compileLoop ----------
struct LoopIndices { int cond_index; int exit_jump_index; };

static LoopIndices compileLoop(const std::vector<std::string>& tokens, size_t &pos,
                               std::vector<Instruction> &bytecode,
                               std::unordered_map<std::string,int> &symTab,
                               int &nextId,
                               const std::unordered_map<std::string,Opcode> &keywordMap)
{
    // tokens[pos] is 'lặp' (OP_LAP)
    auto parenPair = vietvm::compiler::extractParens(tokens, pos + 1); // returns content and index after ')'
    std::string inside = parenPair.first;
    size_t afterParen = parenPair.second;

    // parse inside into 3 parts: init; condition; update
    std::vector<std::string> parts;
    try {
        parts = splitLoopParts(inside);
    } catch (...) {
        std::vector<std::string> tmp;
        std::istringstream iss(inside);
        std::string seg;
        while (std::getline(iss, seg, ';')) {
            tmp.push_back(vietvm::compiler::trim(seg));
        }
        if (tmp.size() >= 3) {
            parts = {tmp[0], tmp[1], tmp[2]};
        } else {
            throw std::runtime_error("compileLoop: cannot parse loop parts");
        }
    }

    // if (!parts[0].empty()) {
    //     std::string varName = extractAssignedVar(parts[0]);
    //     if (!varName.empty()) {
    //         if (symTab.find(varName) == symTab.end()) {
    //             symTab[varName] = nextId++;
    //         }
    //         int varId = symTab[varName];
    //         bytecode.push_back({OP_KHOI_TAO, varId, 0});
    //     }
    //     // compileExpr(parts[0], bytecode, symTab, nextId, keywordMap);
    // }

    // 2) compile init expression
    if (!parts[0].empty()) {
        compileExpr(parts[0], bytecode, symTab, nextId, keywordMap);
    }

    // 3) mark loop start and compile condition
    bytecode.push_back({OP_LAP, 0, 0});
    int cond_index = (int)bytecode.size();
    bytecode.push_back({OP_DIEU_KIEN, 0, 0});
    if (!parts[1].empty()) {
        compileExpr(parts[1], bytecode, symTab, nextId, keywordMap);
    }

    // 4) jump-if-false placeholder
    bytecode.push_back({OP_JUMP_IF_FALSE, 0, 0});
    int exit_jump_index = (int)bytecode.size() - 1;

    // 5) body block
    pos = afterParen;
    if (pos < tokens.size() && tokens[pos] == "{") {
        bytecode.push_back({OP_MO_KHOI, 0, 0});
        compileBlock(tokens, pos, bytecode, symTab, nextId, keywordMap);
        bytecode.push_back({OP_DONG_KHOI, 0, 0});
    }

    // 6) update expression
    bytecode.push_back({OP_CAP_NHAT, 0, 0});
    if (!parts[2].empty()) {
        compileExpr(parts[2], bytecode, symTab, nextId, keywordMap);
    }

    // 7) jump back to condition
    bytecode.push_back({OP_JUMP, cond_index, 0});

    // 8) patch exit jump
    int end_index = (int)bytecode.size();
    bytecode[exit_jump_index].operand = end_index;

    // 9) close loop
    bytecode.push_back({OP_DONG_NGOAC, 0, 0});
    bytecode.push_back({OP_DONG_LENH, 0, 0});

    return {cond_index, exit_jump_index};
}


using CompileFunc = std::function<void(
    const std::vector<std::string>& tokens,
    size_t &pos,
    std::vector<Instruction>& bytecode,
    std::unordered_map<std::string,int>& symTab,
    int& nextId,
    const std::unordered_map<std::string,Opcode>& keywordMap)>;

// Bản đồ ánh xạ keyword → compile function
static std::unordered_map<std::string, CompileFunc> compileMap;

// ---------- compileStatement ----------
static void compileStatement(const std::vector<std::string>& tokens, size_t &pos,
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

    compileMap["nếu"] = compileConditionBlock;
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
}

// ---------- compileBlock (parse and compile until matching '}' ) ----------
// tokens[pos] should be at '{', after this call pos will be index after the matching '}'
static void compileBlock(const std::vector<std::string>& tokens, size_t &pos,
                         std::vector<Instruction> &bytecode,
                         std::unordered_map<std::string,int> &symTab,
                         int &nextId,
                         const std::unordered_map<std::string,Opcode> &keywordMap)
{
    if (pos >= tokens.size() || tokens[pos] != "{") throw std::runtime_error("compileBlock: expected '{'");
    // move past '{'
    ++pos;

    while (pos < tokens.size() && tokens[pos] != "}") {
        compileStatement(tokens, pos, bytecode, symTab, nextId, keywordMap);
    }

    if (pos >= tokens.size() || tokens[pos] != "}") throw std::runtime_error("compileBlock: missing '}'");
    // move pos to token after '}'
    ++pos;
}

// ---------- compileSource: top-level ----------
// This replaces the old line-by-line code and uses token stream + recursive parsing.
// It returns vector<Instruction>.
std::vector<Instruction> compileSource(const std::string& source,
                                       const std::unordered_map<std::string,Opcode>& keywordMap)
{
    std::vector<Instruction> bytecode;
    std::unordered_map<std::string,int> symTab;
    int nextId = 0;

    // Tokenize entire source (supports multi-line)
    auto tokens = vietvm::compiler::tokenize(source);

    size_t pos = 0;
    while (pos < tokens.size()) {
        // skip stray semicolons or closing braces at top-level
        if (tokens[pos] == ";") { ++pos; continue; }
        if (tokens[pos] == "}") { ++pos; continue; }

        compileStatement(tokens, pos, bytecode, symTab, nextId, keywordMap);
    }

    bytecode.push_back({OP_DUNG_CHUONG_TRINH,0,0});
    return bytecode;
}
