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

    size_t pos = 0;
    while (pos < tokens.size()) {
        // skip stray semicolons or closing braces at top-level
        if (tokens[pos] == ";") { ++pos; continue; }
        if (tokens[pos] == "}") { ++pos; continue; }
        std::cerr << "DEBUG tokens.size=" << tokens.size() << ", pos=" << pos << std::endl;
        size_t start = (pos > 5) ? pos - 5 : 0;
        size_t end = std::min(tokens.size(), pos + 6);
        for (size_t i = start; i < end; ++i) {
            std::cerr << "[" << i << "] '" << tokens[i] << "'";
            if (i == pos) std::cerr << "  <-- pos";
            std::cerr << std::endl;
        }
        compileStatement(tokens, pos, bytecode, symTab, nextId, keywordMap);
    }
    auto it = symTab.find("main");
    if (it != symTab.end()) {
        int mainHamId = it->second;
        // Emit OP_GOI with hamId and argc = 0 so VM will run main
        bytecode.push_back({OP_GOI, 0,mainHamId, 0});
        std::cerr << "[compileSource] auto-insert OP_GOI for main hamId=" << mainHamId << std::endl;
    }
    bytecode.push_back({OP_DUNG_CHUONG_TRINH,0,0});
    return bytecode;
}
