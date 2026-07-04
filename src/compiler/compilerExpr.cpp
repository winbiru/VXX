// Created by nx_thang on 10/20/2025.
//
#include "compiler/compilerExpr.h"

#include <algorithm>
#include <sstream>
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
#include "compiler/compileBlock.h"
#include "compiler/compileRegistry.h"


struct Instruction;

static int resolveFunctionIdByNameExpr(const std::string &name,
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

static std::string encodeEscaped(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        if (c == '\\' || c == '\n' || c == '\r' || c == '\t' || c == '\x1e' || c == '\x1f') {
            out.push_back('\\');
            if (c == '\n') out.push_back('n');
            else if (c == '\r') out.push_back('r');
            else if (c == '\t') out.push_back('t');
            else if (c == '\x1e') out.push_back('e');
            else if (c == '\x1f') out.push_back('f');
            else out.push_back('\\');
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

static bool isMapLiteralTokens(const std::vector<std::string> &tokens) {
    return tokens.size() >= 2 && tokens.front() == "{" && tokens.back() == "}";
}

static bool isUnaryMinusContext(const std::string &prev) {
    return prev == "(" || prev == "," || prev == "=" || prev == "+" || prev == "-" ||
           prev == "*" || prev == "/" || prev == "%" || prev == "!" || prev == "&&" ||
           prev == "||" || prev == "==" || prev == "!=" || prev == "<" || prev == ">" ||
           prev == "<=" || prev == ">=";
}

static bool isIdentifierLikeToken(const std::string &tk) {
    if (tk.empty()) return false;
    if (tk == "đúng" || tk == "sai" || tk == "rỗng") return false;
    return vietvm::compiler::isVariable(tk);
}

static bool isMultiWordIdentifier(const std::string &tk) {
    if (tk.find(' ') == std::string::npos) return false;
    std::stringstream ss(tk);
    std::string part;
    bool sawPart = false;
    while (std::getline(ss, part, ' ')) {
        if (part.empty()) continue;
        sawPart = true;
        if (!isIdentifierLikeToken(part)) return false;
    }
    return sawPart;
}

static std::vector<std::string> mergeAdjacentIdentifierTokens(const std::vector<std::string> &tokens) {
    std::vector<std::string> out;
    out.reserve(tokens.size());

    size_t i = 0;
    while (i < tokens.size()) {
        if (!isIdentifierLikeToken(tokens[i])) {
            out.push_back(tokens[i]);
            ++i;
            continue;
        }

        size_t j = i + 1;
        while (j < tokens.size() && isIdentifierLikeToken(tokens[j])) {
            ++j;
        }

        std::string merged = tokens[i];
        for (size_t k = i + 1; k < j; ++k) {
            merged += " ";
            merged += tokens[k];
        }
        out.push_back(merged);
        i = j;
    }

    return out;
}

static std::vector<std::string> mergeUnaryMinusNumbers(const std::vector<std::string> &tokens) {
    std::vector<std::string> out;
    out.reserve(tokens.size());

    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == "-" && (i + 1) < tokens.size()) {
            const std::string &next = tokens[i + 1];
            bool nextIsNumeric = vietvm::compiler::isNumber(next) || vietvm::compiler::isFloat(next);
            bool unaryPos = out.empty() || isUnaryMinusContext(out.back());
            if (nextIsNumeric && unaryPos) {
                out.push_back("-" + next);
                ++i;
                continue;
            }
        }
        out.push_back(tokens[i]);
    }

    return out;
}

struct ParamSpecExpr {
    std::string name;
    bool hasDefault = false;
    std::string defaultEncoded;
};

static std::vector<ParamSpecExpr> parseParamsWithDefaultExpr(const std::string &inside) {
    std::vector<ParamSpecExpr> params;
    std::stringstream ss(inside);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::string token = vietvm::compiler::trim(item);
        if (token.empty()) continue;

        ParamSpecExpr p;
        size_t eq = token.find('=');
        if (eq == std::string::npos) {
            p.name = vietvm::compiler::trim(token);
        } else {
            p.name = vietvm::compiler::trim(token.substr(0, eq));
            std::string dv = vietvm::compiler::trim(token.substr(eq + 1));
            p.hasDefault = true;
            if (vietvm::compiler::isNumber(dv)) p.defaultEncoded = "i:" + dv;
            else if (vietvm::compiler::isFloat(dv)) p.defaultEncoded = "d:" + dv;
            else if (vietvm::compiler::isStringLiteral(dv)) p.defaultEncoded = "s:" + vietvm::compiler::stripQuotes(dv);
            else if (dv == "đúng") p.defaultEncoded = "i:1";
            else if (dv == "sai") p.defaultEncoded = "i:0";
            else if (dv == "rỗng") p.defaultEncoded = "n:";
            else throw std::runtime_error("lambda: tham số mặc định chỉ hỗ trợ literal (int/float/string/đúng/sai/rỗng)");
        }
        if (!p.name.empty()) params.push_back(p);
    }
    return params;
}

static bool tryCompileLambdaLiteral(const std::vector<std::string> &tokens,
                                    std::vector<Instruction> &bytecode,
                                    std::unordered_map<std::string,int> &symTab,
                                    int &nextId,
                                    const std::unordered_map<std::string,Opcode> &keywordMap)
{
    if (tokens.size() < 5) return false;
    if (tokens[0] != "hàm" || tokens[1] != "(") return false;

    auto pr = vietvm::compiler::extractParens(tokens, 1);
    std::string inside = pr.first;
    size_t pos = pr.second;
    if (pos >= tokens.size() || tokens[pos] != "{") return false;

    auto params = parseParamsWithDefaultExpr(inside);

    int hamId = vietvm::compiler::hamMap::allocHamId();

    std::vector<Instruction> funcCode;
    funcCode.push_back({OP_MO_KHOI, 0, 0, 0});

    for (size_t i = 0; i < params.size(); ++i) {
        if (symTab.find(params[i].name) == symTab.end()) symTab[params[i].name] = nextId++;
        int varId = symTab[params[i].name];
        funcCode.push_back({OP_KHOI_TAO, 0, varId, 0});
        if (params[i].hasDefault) {
            int dIdx = vietvm::compiler::StringPool::storeString(params[i].defaultEncoded);
            funcCode.push_back({OP_PARAM_MAC_DINH, dIdx, varId, (int)i});
        } else {
            funcCode.push_back({OP_PARAM, 0, varId, (int)i});
        }
    }

    compileBlock(tokens, pos, funcCode, symTab, nextId, keywordMap);
    funcCode.push_back({OP_DONG_KHOI, 0, 0, 0});
    funcCode.push_back({OP_DONG_LENH, 0, 0, 0});

    vietvm::compiler::hamMap::hamBytecodeMap[hamId] = std::move(funcCode);

    // Lambda value is represented as function id.
    bytecode.push_back({OP_BIEN_SO, hamId, 0, 0});
    return true;
}

static std::string parseAndEncodeMapLiteral(const std::vector<std::string> &tokens) {
    if (!isMapLiteralTokens(tokens)) {
        throw std::runtime_error("Map literal không hợp lệ");
    }

    constexpr char RS = '\x1e'; // record separator
    constexpr char FS = '\x1f'; // field separator

    std::ostringstream encoded;
    bool first = true;

    size_t i = 1; // after '{'
    while (i + 1 < tokens.size()) {
        if (tokens[i] == "}") break;

        std::string key;
        if (vietvm::compiler::isStringLiteral(tokens[i])) {
            key = vietvm::compiler::stripQuotes(tokens[i]);
        } else if (vietvm::compiler::isVariable(tokens[i])) {
            key = tokens[i];
        } else {
            throw std::runtime_error("Map literal: key phải là chuỗi hoặc identifier");
        }
        ++i;

        if (i >= tokens.size() || tokens[i] != ":") {
            throw std::runtime_error("Map literal: thiếu dấu ':' sau key");
        }
        ++i;

        if (i >= tokens.size()) {
            throw std::runtime_error("Map literal: thiếu value");
        }

        std::string typeTag;
        std::string encodedValue;
        const std::string &valTk = tokens[i];
        if (vietvm::compiler::isNumber(valTk)) {
            typeTag = "i";
            encodedValue = valTk;
        } else if (vietvm::compiler::isFloat(valTk)) {
            typeTag = "d";
            encodedValue = valTk;
        } else if (vietvm::compiler::isStringLiteral(valTk)) {
            typeTag = "s";
            encodedValue = vietvm::compiler::stripQuotes(valTk);
        } else if (valTk == "đúng") {
            typeTag = "i";
            encodedValue = "1";
        } else if (valTk == "sai") {
            typeTag = "i";
            encodedValue = "0";
        } else if (valTk == "rỗng") {
            typeTag = "n";
            encodedValue = "";
        } else {
            throw std::runtime_error("Map literal: value chỉ hỗ trợ int/float/string/đúng/sai/rỗng");
        }
        ++i;

        if (!first) encoded << RS;
        first = false;
        encoded << encodeEscaped(key) << FS << typeTag << FS << encodeEscaped(encodedValue);

        if (i < tokens.size() && tokens[i] == ",") {
            ++i;
            continue;
        }
        if (i < tokens.size() && tokens[i] == "}") {
            break;
        }
        if (i < tokens.size() - 1) {
            throw std::runtime_error("Map literal: thiếu dấu ',' giữa các cặp key/value");
        }
    }

    return encoded.str();
}

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
        if (vietvm::compiler::isFloat(tk)) {
            int strIdx = vietvm::compiler::StringPool::storeString(tk);
            bytecode.push_back({OP_BIEN_SO_FLOAT, 0, strIdx, 0});
            continue;
        }
        if (vietvm::compiler::isStringLiteral(tk)) {
            int strIndex = vietvm::compiler::StringPool::storeString(tk.substr(1, tk.size() - 2));
            bytecode.push_back({OP_CHUOI, 0, strIndex,0});
            continue;
        }
        if (tk == "rỗng") {
            bytecode.push_back({OP_RONG_GIA_TRI, 0, 0, 0});
            continue;
        }
        // Boolean literals
        if (tk == "đúng") { bytecode.push_back({OP_BIEN_SO, 1, 0, 0}); continue; }
        if (tk == "sai")  { bytecode.push_back({OP_BIEN_SO, 0, 0, 0}); continue; }

        if (vietvm::compiler::isVariable(tk) || isMultiWordIdentifier(tk)) {
            int maybeFuncId = resolveFunctionIdByNameExpr(tk, symTab);
            if (maybeFuncId >= 0) {
                // Bare function name used as value (higher-order): push function reference.
                bytecode.push_back({OP_BIEN_SO, maybeFuncId, 0, 0});
                continue;
            }

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
            std::string resolvedName = vietvm::compiler::resolveCallableNameInContext(name, symTab);
            vietvm::compiler::validateCallableAccess(resolvedName);
            std::string argcStr = tk.substr(p1 + 2);
            if (argcStr.empty()) throw std::runtime_error("compileExpr: empty argc in CALL token: " + tk);
            int argc = std::stoi(argcStr);

            int hamId = resolveFunctionIdByNameExpr(name, symTab);
            if (hamId >= 0) {
                bytecode.push_back({OP_GOI, argc, hamId, 0});
            } else {
                auto itSym = symTab.find(resolvedName);
                if (itSym == symTab.end()) {
                    int nameIndex = vietvm::compiler::StringPool::storeString(resolvedName);
                    bytecode.push_back({OP_GOI, argc, -(nameIndex + 1), 0});
                } else {
                    int varId = itSym->second;
                    bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, varId, 0});
                    bytecode.push_back({OP_GOI_GIAN_TIEP, argc, 0, 0});
                }
            }
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
    toks = vietvm::compiler::postProcessTokens(toks);
    toks = mergeAdjacentIdentifierTokens(toks);
    toks = mergeUnaryMinusNumbers(toks);

    if (tryCompileLambdaLiteral(toks, bytecode, symTab, nextId, keywordMap)) {
        return;
    }

    // Direct map literal expression: {"a": 1}
    if (isMapLiteralTokens(toks)) {
        std::string encodedMap = parseAndEncodeMapLiteral(toks);
        int mapIndex = vietvm::compiler::StringPool::storeString(encodedMap);
        bytecode.push_back({OP_MAP_LITERAL, 0, mapIndex, 0});
        return;
    }

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

        if (tryCompileLambdaLiteral(rhsTokens, bytecode, symTab, nextId, keywordMap)) {
            bytecode.push_back({OP_TEN_BIEN_ID, 0, dstId,0});
            bytecode.push_back({OP_GAN,0,0,0});
            return;
        }

        if (isMapLiteralTokens(rhsTokens)) {
            std::string encodedMap = parseAndEncodeMapLiteral(rhsTokens);
            int mapIndex = vietvm::compiler::StringPool::storeString(encodedMap);
            bytecode.push_back({OP_MAP_LITERAL, 0, mapIndex, 0});
            bytecode.push_back({OP_TEN_BIEN_ID, 0, dstId,0});
            bytecode.push_back({OP_GAN,0,0,0});
            return;
        }

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