// Compiler.cpp

#include <sstream>
#include <vector>
#include <cctype>
#include <instruction.h>
#include <iostream>
#include <unordered_map>
#include <string>
#include <common/Lex_utils.h>
#include <compiler/compileStatement.h>

#include "compiler/CompileRegistry.h"


// ---------- compileSource: top-level ----------
// This replaces the old line-by-line code and uses token stream + recursive parsing.
// It returns vector<Instruction>.

std::vector<Instruction> compileSource(const std::string& source,
                                       const std::unordered_map<std::string,Opcode>& keywordMap)
{
    std::vector<Instruction> bytecode;
    std::unordered_map<std::string,int> symTab;
    int nextId = 0;

    // Tokenize entire source (supports multi-line)
    auto tokens = vietvm::compiler::tokenize(source);
    tokens = vietvm::compiler::postProcessTokens(tokens);

    // Ensure compileMap has been initialized (register handlers like "hàm", "lặp", ...)
    initCompileMap();

    size_t pos = 0;
    while (pos < tokens.size()) {
        // skip empty/semicolon tokens
        if (tokens[pos].empty() || tokens[pos] == ";") { ++pos; continue; }

        // normalize token for lookup using project helper
        std::string key = vietvm::compiler::normalizeTokenForCompare(tokens[pos]);
        std::string rawToken = tokens[pos];

        // Debug: show token and whether a handler exists
        auto itHandler = compileMap.find(key);
        if (itHandler == compileMap.end()) {
            std::cerr << "[DEBUG compileSource] token at pos=" << pos << " -> '" << rawToken
                      << "' normalized='" << key << "' : NO handler\n";
        } else {
            std::cerr << "[DEBUG compileSource] token at pos=" << pos << " -> '" << rawToken
                      << "' normalized='" << key << "' : handler FOUND\n";
        }

        if (itHandler != compileMap.end()) {
            size_t oldPos = pos;
            itHandler->second(tokens, pos, bytecode, symTab, nextId, keywordMap);
            if (pos == oldPos) {
                std::ostringstream oss;
                oss << "compileSource: handler for '" << tokens[oldPos]
                    << "' did not advance pos (pos=" << pos << ")";
                throw std::runtime_error(oss.str());
            }
            continue;
        }

        // Otherwise treat as a normal statement (assignment/implicit call/expression)
        size_t oldPos = pos;
        compileStatement(tokens, pos, bytecode, symTab, nextId, keywordMap);
        if (pos == oldPos) {
            std::ostringstream oss;
            oss << "compileSource: compileStatement did not advance pos at token '" << tokens[oldPos]
                << "' (pos=" << pos << ")";
            throw std::runtime_error(oss.str());
        }
    }

    // Optional: if there is a global function named "main", emit a call to it before program end.
    // This makes programs with a main() run automatically.
    auto itMain = symTab.find("main");
    if (itMain != symTab.end()) {
        int mainId = itMain->second;
        // emit OP_GOI with hamId in operand and argc = 0 in operandIndex
        bytecode.push_back({OP_GOI, mainId, 0, 0});
        std::cerr << "[DEBUG compileSource] auto-call main hamId=" << mainId << "\n";
    }

    // push program-end instruction (initialize all fields)
    bytecode.push_back({OP_DUNG_CHUONG_TRINH, 0, 0, 0});
    return bytecode;
}