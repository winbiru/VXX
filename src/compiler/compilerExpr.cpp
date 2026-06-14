// Created by nx_thang on 10/20/2025.
//
#include "compiler/compilerExpr.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../include/vm/instruction.h"
#include "common/expression.h"
#include "../../include/frontend/lexer.h"
#include "common/storeString.h"
#include "common/symbolTable.h"
#include "common/utility.h"


struct Instruction;

// Helper: emit operator opcode
static void emitOp(const std::string &tk, std::vector<Instruction> &bytecode) {
    if (tk == "+") bytecode.push_back({OP_CONG,0,0,0});
    else if (tk == "-") bytecode.push_back({OP_TRU,0,0,0});
    else if (tk == "*") bytecode.push_back({OP_NHAN,0,0,0});
    else if (tk == "/") bytecode.push_back({OP_CHIA,0,0,0});
    else if (tk == "%") bytecode.push_back({OP_MODULO,0,0,0});
    else if (tk == "!") bytecode.push_back({OP_PHU_DINH,0,0,0});
    else if (tk == "==") bytecode.push_back({OP_SO_SANH_BANG,0,0,0});
    else if (tk == "!=") bytecode.push_back({OP_KHAC_BANG,0,0,0});
    else if (tk == "<") bytecode.push_back({OP_NHO_HON,0,0,0});
    else if (tk == ">") bytecode.push_back({OP_LON_HON,0,0,0});
    else if (tk == "<=") bytecode.push_back({OP_NHO_HON_HOAC_BANG,0,0,0});
    else if (tk == ">=") bytecode.push_back({OP_LON_HON_HOAC_BANG,0,0,0});
    else if (tk == "&&") bytecode.push_back({OP_Logic_VA,0,0,0});
    else if (tk == "||") bytecode.push_back({OP_Logic_HOAC,0,0,0});
    else if (tk == "++") bytecode.push_back({OP_CONG_MOT,0,0,0});
    else if (tk == "--") bytecode.push_back({OP_TRU_MOT,0,0,0});
    else throw std::runtime_error("compileExpr: unsupported operator " + tk);
}

// Helper: emit postfix tokens (shared by both assignment and non-assignment branches)
static void emitPostfix(const std::vector<std::string> &postfix,
                        std::vector<Instruction> &bytecode,
                        std::unordered_map<std::string,int> &symTab,
                        int &nextId)
{
    for (size_t i = 0; i < postfix.size(); ++i) {
        const auto &tk = postfix[i];

        if (vietvm::compiler::isNumber(tk)) {
            try {
                bytecode.push_back({OP_BIEN_SO, std::stoi(tk), 0,0});
            } catch (...) {
                throw std::runtime_error("compileExpr: isNumber=true but stoi failed for token: '" + tk + "'");
            }
            continue;
        }
        if (vietvm::compiler::isStringLiteral(tk)) {
            int strIndex = vietvm::compiler::StringPool::storeString(tk.substr(1, tk.size() - 2));
            bytecode.push_back({OP_CHUOI, 0, strIndex,0});
            continue;
        }
        // Boolean literals
        if (tk == "đúng") { bytecode.push_back({OP_BIEN_SO, 1, 0, 0}); continue; }
        if (tk == "sai")  { bytecode.push_back({OP_BIEN_SO, 0, 0, 0}); continue; }

        if (vietvm::compiler::isVariable(tk)) {
            int id = vietvm::compiler::symbolTable::getOrCreate(symTab, tk, nextId);
            // Postfix ++ or --: emit variable ID so handler can update in place
            if (i + 1 < postfix.size() && (postfix[i + 1] == "++" || postfix[i + 1] == "--")) {
                bytecode.push_back({OP_TEN_BIEN_ID, 0, id,0});
                continue;
            }
            bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, id,0});
            continue;
        }
        if (tk.rfind("CALL::", 0) == 0) {
            size_t p1 = tk.find("::", 6);
            if (p1 == std::string::npos) throw std::runtime_error("compileExpr: malformed CALL token");
            std::string name = tk.substr(6, p1 - 6);
            std::string argcStr = tk.substr(p1 + 2);
            if (argcStr.empty()) throw std::runtime_error("compileExpr: empty argc in CALL token: " + tk);
            int argc = std::stoi(argcStr);
            int nameIndex = vietvm::compiler::StringPool::storeString(name);
            bytecode.push_back({OP_GOI, argc, nameIndex,0});
            continue;
        }

        emitOp(tk, bytecode);
    }
}

// ---------- compileExpr: produce bytecode for an expression or assignment ----------
void compileExpr(const std::string &expr,
                        std::vector<Instruction> &bytecode,
                        std::unordered_map<std::string,int> &symTab,
                        int &nextId,
                        const std::unordered_map<std::string,Opcode> &keywordMap)
{
    (void)keywordMap;
    if (vietvm::compiler::trim(expr).empty()) return;

    auto toks = vietvm::compiler::tokenize(expr);

    // ---- Compound assignments: x += e, x -= e, x *= e, x /= e, x %= e ----
    // Pattern: toks[0] = varName, toks[1] = op=, rest = rhs
    if (toks.size() >= 3) {
        const std::string &op2 = toks[1];
        if (op2 == "+=" || op2 == "-=" || op2 == "*=" || op2 == "/=" || op2 == "%=") {
            std::string varName = toks[0];
            int dstId = vietvm::compiler::symbolTable::getOrCreate(symTab, varName, nextId);
            // Desugar: varName op= rhs → varName = varName op rhs
            // First push current value of varName
            bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, dstId, 0});
            // Compile rhs
            std::vector<std::string> rhsToks(toks.begin() + 2, toks.end());
            auto postfix = vietvm::compiler::convertToPostfix(rhsToks);
            emitPostfix(postfix, bytecode, symTab, nextId);
            // Emit arithmetic op
            if (op2 == "+=")      bytecode.push_back({OP_CONG,0,0,0});
            else if (op2 == "-=") bytecode.push_back({OP_TRU,0,0,0});
            else if (op2 == "*=") bytecode.push_back({OP_NHAN,0,0,0});
            else if (op2 == "/=") bytecode.push_back({OP_CHIA,0,0,0});
            else if (op2 == "%=") bytecode.push_back({OP_MODULO,0,0,0});
            // Store result
            bytecode.push_back({OP_TEN_BIEN_ID, 0, dstId, 0});
            bytecode.push_back({OP_GAN,0,0,0});
            return;
        }
    }

    // ---- Simple assignment: varName = rhs ----
    auto itEq = std::find(toks.begin(), toks.end(), "=");

    if (itEq != toks.end() && std::distance(toks.begin(), itEq) == 1) {
        std::string varName = toks[0];
        int dstId = vietvm::compiler::symbolTable::getOrCreate(symTab, varName, nextId);

        std::vector<std::string> rhsTokens(itEq + 1, toks.end());
        auto postfix = vietvm::compiler::convertToPostfix(rhsTokens);
        emitPostfix(postfix, bytecode, symTab, nextId);

        bytecode.push_back({OP_TEN_BIEN_ID, 0, dstId,0});
        bytecode.push_back({OP_GAN,0,0,0});
        return;
    }

    // ---- Non-assignment expression ----
    auto postfix = vietvm::compiler::convertToPostfix(toks);
    emitPostfix(postfix, bytecode, symTab, nextId);
}