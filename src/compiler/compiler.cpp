// Compiler.cpp

#include <sstream>
#include <vector>
#include <cctype>
#include <../include/vm/instruction.h>
#include <iostream>
#include <unordered_map>
#include <string>
#include <functional>
#include <../include/frontend/lexer.h>
#include <compiler/compileStatement.h>
#include "common/storeString.h"


// ---------- compileSource: top-level ----------
// This replaces the old line-by-line code and uses token stream + recursive parsing.
// It returns vector<Instruction>.

static std::vector<Instruction> optimizeBytecodePeephole(const std::vector<Instruction> &input) {
    if (input.empty()) return input;

    const int n = (int)input.size();
    std::vector<char> keep(n, 1);

    // Remove no-op syntax markers that VM ignores at runtime.
    for (int i = 0; i < n; ++i) {
        Opcode op = input[i].op;
        if (op == OP_DONG_LENH || op == OP_MO_NGOAC || op == OP_DONG_NGOAC) {
            keep[i] = 0;
        }
    }

    bool changed = false;
    for (int i = 0; i < n; ++i) {
        if (!keep[i]) { changed = true; break; }
    }
    if (!changed) return input;

    std::vector<int> oldToNew(n, -1);
    int newCount = 0;
    for (int i = 0; i < n; ++i) {
        if (keep[i]) oldToNew[i] = newCount++;
    }

    std::vector<int> nextKeptOld(n + 1, n);
    nextKeptOld[n] = n;
    for (int i = n - 1; i >= 0; --i) {
        nextKeptOld[i] = keep[i] ? i : nextKeptOld[i + 1];
    }

    auto mapTarget = [&](int oldTarget) -> int {
        if (oldTarget < 0) return oldTarget;
        if (oldTarget >= n) return newCount;
        int keptAtOrAfter = nextKeptOld[oldTarget];
        if (keptAtOrAfter >= n) return newCount;
        return oldToNew[keptAtOrAfter];
    };

    std::vector<Instruction> out;
    out.reserve((size_t)newCount);
    for (int i = 0; i < n; ++i) {
        if (!keep[i]) continue;
        Instruction inst = input[i];
        if (inst.op == OP_JUMP || inst.op == OP_JUMP_IF_FALSE || inst.op == OP_THU || inst.op == OP_THU_KET_THUC) {
            inst.operand = mapTarget(inst.operand);
        }
        out.push_back(inst);
    }
    return out;
}

std::vector<Instruction> compileSource(const std::string& source,
                                       const std::unordered_map<std::string,Opcode>& keywordMap,
                                       bool emitMainCall)
{
    std::vector<Instruction> bytecode;
    std::unordered_map<std::string,int> symTab;
    int nextId = 0;

    // Tokenize entire source (supports multi-line)
    auto tokens = vietvm::compiler::tokenize(source);
    tokens = vietvm::compiler::postProcessTokens(tokens);

    // Predeclare top-level functions so forward calls resolve by name reliably.
    int topLevelDepth = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == "{") { ++topLevelDepth; continue; }
        if (tokens[i] == "}") { if (topLevelDepth > 0) --topLevelDepth; continue; }

        if (topLevelDepth == 0 && tokens[i] == "hàm" && i + 1 < tokens.size()) {
            const std::string &fname = tokens[i + 1];
            if (symTab.find(fname) == symTab.end()) {
                int hamId = vietvm::compiler::hamMap::allocHamId();
                symTab[fname] = hamId;
                int nameIndex = vietvm::compiler::StringPool::storeString(fname);
                vietvm::compiler::hamMap::hamBytecodeMap[hamId] = {};
                vietvm::compiler::hamMap::setHamNameIndex(hamId, nameIndex);
            }
        }
    }

    // Keep variable IDs in a separate numeric range from predeclared function IDs.
    for (const auto &kv : symTab) {
        if (kv.second >= nextId) nextId = kv.second + 1;
    }

    size_t pos = 0;
    while (pos < tokens.size()) {
        // skip stray semicolons or closing braces at top-level
        if (tokens[pos] == ";") { ++pos; continue; }
        if (tokens[pos] == "}") { ++pos; continue; }
        compileStatement(tokens, pos, bytecode, symTab, nextId, keywordMap);
    }

    // Apply peephole optimization to compiled functions and top-level code.
    for (auto &kv : vietvm::compiler::hamMap::hamBytecodeMap) {
        kv.second = optimizeBytecodePeephole(kv.second);
    }
    bytecode = optimizeBytecodePeephole(bytecode);

    if (emitMainCall) {
        auto it = symTab.find("main");
        if (it != symTab.end()) {
            int mainHamId = it->second;
            // Emit OP_GOI with hamId and argc = 0 so VM will run main
            bytecode.push_back({OP_GOI, 0,mainHamId, 0});
        }
        bytecode.push_back({OP_DUNG_CHUONG_TRINH,0,0,0});
    }
    return bytecode;
}
