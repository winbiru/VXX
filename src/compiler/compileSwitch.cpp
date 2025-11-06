//
// Created by nx_thang on 10/21/2025.
//

#include "compiler/compileSwitch.h"
#include "compiler/compileSwitch.h"

#include <iostream>
#include <ostream>
#include <stdexcept>

#include "compiler/compilerExpr.h"
#include "compiler/compileBlock.h"
#include "../../include/frontend/lexer.h"
#include "common/storeString.h"
#include "common/utility.h"

void compileSwitch(const std::vector<std::string>& tokens, size_t &pos,
                   std::vector<Instruction>& bytecode,
                   std::unordered_map<std::string,int>& symTab,
                   int& nextId,
                   const std::unordered_map<std::string,Opcode>& keywordMap)
{
    if (pos >= tokens.size() || vietvm::compiler::normalizeTokenForCompare(tokens[pos]) != "chọn") {
        throw std::runtime_error("compileSwitch: không phải token 'chọn' tại vị trí pos");
    }

    ++pos; // bỏ qua "chọn"
    auto pr = vietvm::compiler::extractParens(tokens, pos);
    std::string expr = pr.first;
    pos = pr.second;

    compileExpr(expr, bytecode, symTab, nextId, keywordMap);
    bytecode.push_back({OP_CHON, 0, 0,0});

    if (pos >= tokens.size() || tokens[pos] != "{") throw std::runtime_error("compileSwitch: thiếu dấu '{'");
    ++pos;

    while (pos < tokens.size() && tokens[pos] != "}") {
        // Skip stray semicolons or empty/normalized-empty tokens that may appear between cases
        if (tokens[pos] == ";") {
            ++pos;
            continue;
        }
        std::string curNorm = vietvm::compiler::normalizeTokenForCompare(tokens[pos]);

        // If normalize returns empty (e.g. token was just punctuation), skip it safely.
        if (curNorm.empty()) {
            ++pos;
            continue;
        }

        // xử lý ca
        if (curNorm == "ca") {
            ++pos; // tiêu thụ "ca"
            if (pos >= tokens.size()) throw std::runtime_error("compileSwitch: thiếu biểu thức sau 'ca'");

            // Nếu tokenizer tách "mặc" và "định" thành 2 token, normalize sẽ xử lý ở nhánh mặc định bên dưới.
            std::string caseToken = tokens[pos];
            std::string caseNorm = vietvm::compiler::normalizeTokenForCompare(caseToken);

            // Nếu gặp "mặc" (tách) và token sau là "định", ghép thành mặc định
            if (caseNorm == "mặc" && pos + 1 < tokens.size()) {
                std::string nextNorm = vietvm::compiler::normalizeTokenForCompare(tokens[pos + 1]);
                if (nextNorm == "định") {
                    // xử lý mặc định
                    bytecode.push_back({OP_MAC_DINH, 0, 0,0});
                    pos += 2; // bỏ "mặc" và "định"
                    // bỏ qua dấu ':' nếu có
                    if (pos < tokens.size() && tokens[pos] == ":") ++pos;
                    if (pos < tokens.size() && tokens[pos] == "{") {
                        bytecode.push_back({OP_MO_KHOI, 0, 0,0});
                        compileBlock(tokens, pos, bytecode, symTab, nextId, keywordMap);
                        bytecode.push_back({OP_DONG_KHOI, 0, 0,0});
                    }
                    continue;
                }
            }

            // Nếu token là "mặc định" nguyên vẹn (có thể có dấu ':')
            if (caseNorm == "mặc định") {
                bytecode.push_back({OP_MAC_DINH, 0, 0,0});
                ++pos;
                if (pos < tokens.size() && tokens[pos] == ":") ++pos;
                if (pos < tokens.size() && tokens[pos] == "{") {
                    bytecode.push_back({OP_MO_KHOI, 0, 0,0});
                    compileBlock(tokens, pos, bytecode, symTab, nextId, keywordMap);
                    bytecode.push_back({OP_DONG_KHOI, 0, 0,0});
                }
                continue;
            }

            // Bình thường: caseExpr có thể là string literal, number, hoặc biến
            // Lấy nguyên token (không normalize nếu là string literal)
            std::string caseExpr = tokens[pos++];
            // nếu token có dấu ':' dính (ví dụ 9:), loại bỏ ':' trước parse số/biến
            std::string caseExprNorm = vietvm::compiler::normalizeTokenForCompare(caseExpr);

            if (vietvm::compiler::isStringLiteral(caseExpr)) {
                std::string raw = vietvm::compiler::stripQuotes(caseExpr);
                int strIndex = vietvm::compiler::StringPool::storeString(raw);
                bytecode.push_back({OP_CA, 0, strIndex,0}); // operandIndex = chuỗi
            } else if (vietvm::compiler::isNumber(caseExprNorm)) {
                int value = std::stoi(caseExprNorm);
                bytecode.push_back({OP_CA, value, -1,0}); // operand = số, operandIndex = -1
            } else {
                // giả sử là biến (sử dụng caseExprNorm)
                std::string varName = caseExprNorm;
                if (symTab.find(varName) == symTab.end()) {
                    symTab[varName] = nextId++;
                }
                int varId = symTab[varName];
                bytecode.push_back({OP_CA, varId, -2,0}); // operand = id biến, operandIndex = -2
            }

            // bỏ qua ':' nếu có
            if (pos < tokens.size() && tokens[pos] == ":") ++pos;

            // nếu có block ngay sau case
            if (pos < tokens.size() && tokens[pos] == "{") {
                bytecode.push_back({OP_MO_KHOI, 0, 0,0});
                compileBlock(tokens, pos, bytecode, symTab, nextId, keywordMap);
                bytecode.push_back({OP_DONG_KHOI, 0, 0,0});
            }

        } else if (curNorm == "mặc định") {
            // trường hợp tokenizer không đặt "ca" trước (không nên), nhưng vẫn xử lý an toàn
            bytecode.push_back({OP_MAC_DINH, 0, 0,0});
            ++pos;
            if (pos < tokens.size() && tokens[pos] == ":") ++pos;
            if (pos < tokens.size() && tokens[pos] == "{") {
                bytecode.push_back({OP_MO_KHOI, 0, 0,0});
                compileBlock(tokens, pos, bytecode, symTab, nextId, keywordMap);
                bytecode.push_back({OP_DONG_KHOI, 0, 0,0});
            }

        } else if (curNorm == "thoát") {
            bytecode.push_back({OP_THOAT, 0, 0,0});
            ++pos;
        } else {
            std::cerr << "Token không hợp lệ trong khối chọn: '" << tokens[pos] << "' (normalized='" << curNorm << "')" << std::endl;
            throw std::runtime_error("compileSwitch: từ khóa không hợp lệ trong khối chọn");
        }
    }

    if (pos >= tokens.size() || tokens[pos] != "}") throw std::runtime_error("compileSwitch: thiếu dấu '}'");
    ++pos;
}