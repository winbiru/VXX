//
// Created by nx_thang on 10/21/2025.
//

#include "compiler/compileBlock.h"

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <iostream> // debug
#include <sstream>

#include "../../include/compiler/compileStatement.h"
#include "instruction.h"
#include "compiler/compileRegistry.h" // for compileMap
#include "../../include/frontend/lexer.h"

// Helper to produce a small window of tokens around pos for debugging
static std::string tokens_context(const std::vector<std::string>& tokens, size_t pos, size_t window = 8) {
    std::ostringstream oss;
    size_t start = (pos > window) ? pos - window : 0;
    size_t end = std::min(tokens.size(), pos + window);
    oss << "tokens[" << start << ".." << end-1 << "]:";
    for (size_t i = start; i < end; ++i) {
        if (i == pos) oss << " >>[" << tokens[i] << "]<<";
        else oss << " " << tokens[i];
    }
    return oss.str();
}

void compileBlock(const std::vector<std::string>& tokens, size_t &pos,
                         std::vector<Instruction> &bytecode,
                         std::unordered_map<std::string,int> &symTab,
                         int &nextId,
                         const std::unordered_map<std::string,Opcode> &keywordMap)
{
    if (pos >= tokens.size() || tokens[pos] != "{") {
        std::ostringstream msg;
        msg << "compileBlock: expected '{' at pos=" << pos;
        if (pos < tokens.size()) msg << ", found token='" << tokens[pos] << "'";
        throw std::runtime_error(msg.str());
    }
    // move past '{'
    ++pos;

    while (pos < tokens.size() && tokens[pos] != "}") {
        // skip empties and stray semicolons inside blocks
        if (tokens[pos].empty() || tokens[pos] == ";") { ++pos; continue; }

        // normalize token for lookup using project's normalization
        std::string key = vietvm::compiler::normalizeTokenForCompare(tokens[pos]);

        auto itHandler = compileMap.find(key);
        if (itHandler != compileMap.end()) {
            size_t oldPos = pos;
            itHandler->second(tokens, pos, bytecode, symTab, nextId, keywordMap);
            if (pos == oldPos) {
                std::ostringstream oss;
                oss << "compileBlock: handler for '" << tokens[oldPos] << "' did not advance pos (pos=" << pos << ")\nContext: "
                    << tokens_context(tokens, oldPos);
                throw std::runtime_error(oss.str());
            }
            continue;
        }

        // Otherwise treat as a normal statement (assignment/implicit call/expression)
        size_t oldPos = pos;
        compileStatement(tokens, pos, bytecode, symTab, nextId, keywordMap);
        if (pos == oldPos) {
            std::ostringstream oss;
            oss << "compileBlock: compileStatement did not advance pos at token '" << tokens[oldPos]
                << "' (pos=" << pos << ")\nContext: " << tokens_context(tokens, oldPos);
            throw std::runtime_error(oss.str());
        }
    }

    if (pos >= tokens.size() || tokens[pos] != "}") {
        // Provide rich debug info to locate cause
        std::ostringstream oss;
        oss << "compileBlock: missing '}' at pos=" << pos << ". ";
        if (!tokens.empty()) {
            oss << "Token count=" << tokens.size() << ". ";
            if (pos < tokens.size()) {
                oss << "Token at pos: '" << tokens[pos] << "'. ";
            } else {
                oss << "pos is beyond tokens (pos >= tokens.size()). ";
            }
            oss << "\nContext: " << tokens_context(tokens, (pos < tokens.size() ? pos : tokens.size()-1));
        }
        // Also print to stderr for immediate visibility in console
        std::cerr << oss.str() << std::endl;
        throw std::runtime_error(oss.str());
    }
    // move pos to token after '}'
    ++pos;
}