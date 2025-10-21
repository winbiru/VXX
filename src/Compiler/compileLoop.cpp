//
// Created by nx_thang on 10/21/2025.
//

#include "compiler/compileLoop.h"

#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "instruction.h"
#include "common/LoopUltil.h"
#include "common/Utility.h"
#include "compiler/compileBlock.h"
#include "compiler/compilerExpr.h"

LoopIndices compileLoop(const std::vector<std::string>& tokens, size_t &pos,
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
