//
// Created by nx_thang on 10/21/2025.
//

#include "compiler/compileBlock.h"

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../include/compiler/compileStatement.h"
#include "instruction.h"

void compileBlock(const std::vector<std::string>& tokens, size_t &pos,
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
