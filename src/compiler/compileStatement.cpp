//
// Created by nx_thang on 10/21/2025.
//

#include "../../include/compiler/compileStatement.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <common/utility.h>
#include <compiler/compileBlock.h>
#include <compiler/compileRegistry.h>
#include <compiler/compilerExpr.h>
#include "common/storeString.h"
#include "common/symbolTable.h"
#include "vpp/core/message_constants.h"
#include <sstream>
#include <iostream>
#include "../../include/frontend/lexer.h"

static bool tryParseCallableNameBeforeParen(const std::vector<std::string> &tokens,
                                            size_t start,
                                            size_t &parenPos,
                                            std::string &nameOut) {
    if (start >= tokens.size() || !vietvm::compiler::isCallableNamePiece(tokens[start])) return false;

    size_t i = start;
    while (i < tokens.size()) {
        if (tokens[i] == "(") {
            parenPos = i;
            nameOut = vietvm::compiler::joinNameTokens(tokens, start, i);
            return !nameOut.empty();
        }

        if (tokens[i] == ";" || tokens[i] == "," || tokens[i] == "=" ||
            tokens[i] == "{" || tokens[i] == "}" || tokens[i] == "[" || tokens[i] == "]" ||
            tokens[i] == ")" || vietvm::compiler::isOperator(tokens[i])) {
            return false;
        }

        if (!vietvm::compiler::isCallableNamePiece(tokens[i])) {
            return false;
        }
        ++i;
    }

    return false;
}

void compileStatement(const std::vector<std::string>& tokens, size_t &pos,
                      std::vector<Instruction> &bytecode,
                      std::unordered_map<std::string,int> &symTab,
                      int &nextId,
                      const std::unordered_map<std::string,Opcode> &keywordMap)
{
    if (pos >= tokens.size()) return;
    const std::string &tk = tokens[pos];

    if (vietvm::compiler::isVisibilityToken(tk) &&
        (pos + 1) < tokens.size() && tokens[pos + 1] == "hàm") {
        throw std::runtime_error(vietvm::messages::formatMessage(
            vietvm::messages::kSyntaxModifierBeforeFunction, {tk}));
    }

    // Defensive fallback: ensure "trả về" never falls through to expression parsing.
    // This prevents convertToPostfix errors when token normalization varies by context.
    if (tk == "trả về") {
        auto itReturn = compileMap.find("trả về");
        if (itReturn != compileMap.end()) {
            itReturn->second(tokens, pos, bytecode, symTab, nextId, keywordMap);
            return;
        }
    }
    if (tk == "trả" && (pos + 1) < tokens.size() && tokens[pos + 1] == "về") {
        {
            auto itReturn = compileMap.find("trả");
            if (itReturn != compileMap.end()) {
                itReturn->second(tokens, pos, bytecode, symTab, nextId, keywordMap);
                return;
            }
        }
    }

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
    size_t callParenPos = 0;
    std::string ident;
    if (tryParseCallableNameBeforeParen(tokens, pos, callParenPos, ident)) {
        std::string resolvedIdent = vietvm::compiler::resolveCallableNameInContext(ident, symTab);
        vietvm::compiler::validateCallableAccess(resolvedIdent);

        // extract content inside parens; extractParens expects the position of '('
        auto pr = vietvm::compiler::extractParens(tokens, callParenPos);
        std::string inside = pr.first;
        pos = pr.second; // pos now points after ')'

        // split args and compile each expression
        std::vector<std::string> args = vietvm::compiler::splitTopLevelArguments(inside);
        int compiledArgs = 0;
        for (const auto &aexpr : args) {
            if (aexpr.empty()) continue;
            compileExpr(aexpr, bytecode, symTab, nextId, keywordMap);
            ++compiledArgs;
        }

        // Direct call for known function id; otherwise indirect call via variable value.
        int hamId = vietvm::compiler::resolveFunctionIdByName(ident, symTab);

        if (hamId >= 0) {
            bytecode.push_back({OP_GOI, compiledArgs, hamId, 0});
        } else {
            auto itSym = symTab.find(resolvedIdent);
            if (itSym == symTab.end()) {
                int nameIndex = vietvm::compiler::StringPool::storeString(resolvedIdent);
                bytecode.push_back({OP_GOI, compiledArgs, -(nameIndex + 1), 0});
            } else {
                int varId = itSym->second;
                bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, varId, 0});
                bytecode.push_back({OP_GOI_GIAN_TIEP, compiledArgs, 0, 0});
            }
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
