//
// Created by nx_thang on 10/21/2025.
//

#include "compiler/compileBlock.h"

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>

#include "../../include/compiler/compileStatement.h"
#include "../../include/vm/instruction.h"
#include "compiler/compileRegistry.h" // for compileMap
#include "../../include/frontend/lexer.h"
#include "vpp/core/message_constants.h"

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
        const std::string foundToken = pos < tokens.size() ? tokens[pos] : "EOF";
        throw std::runtime_error(vietvm::messages::formatMessage(
            vietvm::messages::kSyntaxExpectedBlockAtPosition,
            {std::to_string(pos), foundToken}));
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
                throw std::runtime_error(vietvm::messages::formatMessage(
                    vietvm::messages::kInternalBlockHandlerDidNotAdvance,
                    {tokens[oldPos], std::to_string(pos), tokens_context(tokens, oldPos)}));
            }
            continue;
        }

        // Otherwise treat as a normal statement (assignment/implicit call/expression)
        size_t oldPos = pos;
        compileStatement(tokens, pos, bytecode, symTab, nextId, keywordMap);
        if (pos == oldPos) {
            throw std::runtime_error(vietvm::messages::formatMessage(
                vietvm::messages::kInternalStatementDidNotAdvance,
                {tokens[oldPos], std::to_string(pos), tokens_context(tokens, oldPos)}));
        }
    }

    if (pos >= tokens.size() || tokens[pos] != "}") {
        const std::string foundToken = pos < tokens.size() ? tokens[pos] : "EOF";
        const std::string context = tokens.empty()
            ? std::string()
            : tokens_context(tokens, pos < tokens.size() ? pos : tokens.size() - 1);
        throw std::runtime_error(vietvm::messages::formatMessage(
            vietvm::messages::kInternalMissingClosingBlock,
            {std::to_string(pos), std::to_string(tokens.size()), foundToken, context}));
    }
    // move pos to token after '}'
    ++pos;
}
