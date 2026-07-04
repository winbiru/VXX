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
#include <sstream>
#include <iostream>
#include "../../include/frontend/lexer.h"

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

static int resolveFunctionIdByName(const std::string &name,
                                   const std::unordered_map<std::string,int> &symTab) {
    std::string resolvedName = vietvm::compiler::resolveCallableNameInContext(name, symTab);
    vietvm::compiler::validateCallableAccess(resolvedName);

    auto idMatchesResolvedName = [&](int candidateId) {
        int resolvedNameIndex = vietvm::compiler::StringPool::findString(resolvedName);
        if (resolvedNameIndex < 0) return false;
        auto itName = vietvm::compiler::hamMap::hamNameIndexMap.find(candidateId);
        if (itName == vietvm::compiler::hamMap::hamNameIndexMap.end()) return false;
        return itName->second == resolvedNameIndex;
    };

    auto itSym = symTab.find(resolvedName);
    if (itSym != symTab.end()) {
        int maybeId = itSym->second;
        auto itCode = vietvm::compiler::hamMap::hamBytecodeMap.find(maybeId);
        if (itCode != vietvm::compiler::hamMap::hamBytecodeMap.end() &&
            !itCode->second.empty() &&
            idMatchesResolvedName(maybeId)) {
            return maybeId;
        }
    }

    int nameIndex = vietvm::compiler::StringPool::findString(resolvedName);
    if (nameIndex >= 0) {
        for (const auto &kv : vietvm::compiler::hamMap::hamNameIndexMap) {
            if (kv.second == nameIndex) return kv.first;
        }
    }
    return -1;
}

static std::string joinNameTokens(const std::vector<std::string> &tokens, size_t begin, size_t end) {
    std::string out;
    for (size_t i = begin; i < end; ++i) {
        if (!out.empty()) out.push_back(' ');
        out += tokens[i];
    }
    return out;
}

static bool isCallableNamePiece(const std::string &token) {
    if (vietvm::compiler::isVariable(token)) return true;
    if (token.find(' ') == std::string::npos) return false;
    std::stringstream ss(token);
    std::string part;
    while (std::getline(ss, part, ' ')) {
        if (part.empty()) continue;
        if (!vietvm::compiler::isVariable(part)) return false;
    }
    return true;
}

static bool tryParseCallableNameBeforeParen(const std::vector<std::string> &tokens,
                                            size_t start,
                                            size_t &parenPos,
                                            std::string &nameOut) {
    if (start >= tokens.size() || !isCallableNamePiece(tokens[start])) return false;

    size_t i = start;
    while (i < tokens.size()) {
        if (tokens[i] == "(") {
            parenPos = i;
            nameOut = joinNameTokens(tokens, start, i);
            return !nameOut.empty();
        }

        if (tokens[i] == ";" || tokens[i] == "," || tokens[i] == "=" ||
            tokens[i] == "{" || tokens[i] == "}" || tokens[i] == "[" || tokens[i] == "]" ||
            tokens[i] == ")" || vietvm::compiler::isOperator(tokens[i])) {
            return false;
        }

        if (!isCallableNamePiece(tokens[i])) {
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
        throw std::runtime_error(
            "Dùng cú pháp 'hàm <quyền>' (ví dụ: 'hàm " + tk +
            " tenHam(...)') thay vì '<quyền> hàm'");
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
        std::vector<std::string> args = splitArgsString(inside);
        int compiledArgs = 0;
        for (const auto &aexpr : args) {
            if (aexpr.empty()) continue;
            compileExpr(aexpr, bytecode, symTab, nextId, keywordMap);
            ++compiledArgs;
        }

        // Direct call for known function id; otherwise indirect call via variable value.
        int hamId = resolveFunctionIdByName(ident, symTab);

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
