// Compiler.cpp

#include "../../include/compiler/compiler.h"
#include <sstream>
#include <vector>
#include <cctype>
#include <unordered_map>
#include "../../include/instruction.h"
#include <string>
#include "../../include/common/Lex_utils.h"
#include "../../include/compiler/compileStatement.h"


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

        compileStatement(tokens, pos, bytecode, symTab, nextId, keywordMap);
    }

    bytecode.push_back({OP_DUNG_CHUONG_TRINH,0,0});
    return bytecode;
}
