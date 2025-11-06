//
// Created by nx_thang on 10/20/2025.
//
#include "compiler/compilerExpr.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "instruction.h"
#include "common/Expression.h"
#include "common/Lex_utils.h"
#include "common/storeString.h"
#include "common/SymbolTable.h"
#include "common/Utility.h"


struct Instruction;
// ---------- compileExpr: produce bytecode for an expression or assignment ----------
void compileExpr(const std::string &expr,
                        std::vector<Instruction> &bytecode,
                        std::unordered_map<std::string,int> &symTab,
                        int &nextId,
                        const std::unordered_map<std::string,Opcode> &keywordMap)
{
    (void)keywordMap; // không dùng ở handler này
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
                bytecode.push_back({OP_BIEN_SO, std::stoi(tk), 0,0});
            } else if (vietvm::compiler::isStringLiteral(tk)) {
                int strIndex = vietvm::compiler::StringPool::storeString(tk.substr(1, tk.size() - 2));
                bytecode.push_back({OP_CHUOI, 0, strIndex,0});
            } else if (vietvm::compiler::isVariable(tk)) {
                int id = vietvm::compiler::SymbolTable::getOrCreate(symTab, tk, nextId);
                // ensureInitialized(id);
                bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, id,0});
            } else if (tk.rfind("CALL::", 0) == 0) {
                // format: CALL::name::argc
                size_t p1 = tk.find("::", 6); // find second ::
                if (p1 == std::string::npos) throw std::runtime_error("compileExpr: malformed CALL token");
                std::string name = tk.substr(6, p1 - 6);
                std::string argcStr = tk.substr(p1 + 2);
                int argc = std::stoi(argcStr);
                int nameIndex = vietvm::compiler::StringPool::storeString(name);
                bytecode.push_back({OP_GOI, argc, nameIndex,0});
            } else {
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
                else throw std::runtime_error("compileExpr: unsupported operator " + tk);
            }
        }

        bytecode.push_back({OP_TEN_BIEN_ID, 0, dstId,0});
        bytecode.push_back({OP_GAN,0,0,0});
        return;
    }

    auto postfix = vietvm::compiler::convertToPostfix(toks);
    for (const auto &tk : postfix) {
        if (vietvm::compiler::isNumber(tk)) {
            bytecode.push_back({OP_BIEN_SO, std::stoi(tk), 0,0});
        } else if (vietvm::compiler::isStringLiteral(tk)) {
            int strIndex = vietvm::compiler::StringPool::storeString(tk.substr(1, tk.size() - 2));
            bytecode.push_back({OP_CHUOI, 0, strIndex,0});
        } else if (vietvm::compiler::isVariable(tk)) {
            int id = vietvm::compiler::SymbolTable::getOrCreate(symTab, tk, nextId);
            // ensureInitialized(id);
            bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, id,0});
        } else if (tk.rfind("CALL::", 0) == 0) {
            // format: CALL::name::argc
            size_t p1 = tk.find("::", 6); // find second ::
            if (p1 == std::string::npos) throw std::runtime_error("compileExpr: malformed CALL token");
            std::string name = tk.substr(6, p1 - 6);
            std::string argcStr = tk.substr(p1 + 2);
            int argc = std::stoi(argcStr);
            int nameIndex = vietvm::compiler::StringPool::storeString(name);
            bytecode.push_back({OP_GOI, argc, nameIndex,0});
        } else {
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
            else throw std::runtime_error("compileExpr: unsupported token " + tk);
        }
    }
}