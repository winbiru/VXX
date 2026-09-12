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
#include <compiler/compileRegistry.h>
#include "common/storeString.h"
#include "common/utility.h"
#include "vpp/compiler/pipeline.h"

namespace vietvm::compiler {

void resetCompilationState() {
    StringPool::clear();
    clearImportedFiles();
    clearClassAccessState();
    hamMap::hamBytecodeMap.clear();
    hamMap::clearHamNameIndexMap();
    hamMap::resetHamIdCounter();
}

void CompilationContext::clear() {
    stringPool.clear();
    functionBytecode.clear();
    functionNameIndices.clear();
}

} // namespace vietvm::compiler

// ---------- legacy bytecode lowering ----------
// Features not yet handled by the direct IR emitter still use this established
// VM backend. Its input is materialized from IR rather than read directly from
// the lexer, preserving behavior while fallback coverage shrinks.

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

static std::vector<Instruction> compileLegacyTokens(
    const std::vector<std::string> &tokens,
    const std::unordered_map<std::string,Opcode> &keywordMap,
    bool emitMainCall)
{
    std::vector<Instruction> bytecode;
    std::unordered_map<std::string,int> symTab;
    int nextId = 0;

    // compileMap was historically initialized by the CLI.  Keep direct API
    // users safe now that compilation can start at the pipeline boundary.
    if (compileMap.empty()) initCompileMap();

    // Predeclare top-level functions so forward calls resolve by name reliably.
    int topLevelDepth = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == "{") { ++topLevelDepth; continue; }
        if (tokens[i] == "}") { if (topLevelDepth > 0) --topLevelDepth; continue; }

        if (topLevelDepth == 0 && tokens[i] == "hàm" && i + 1 < tokens.size()) {
            size_t namePos = i + 1;
            if (tokens[namePos] == "công khai" || tokens[namePos] == "riêng tư" || tokens[namePos] == "bảo vệ") {
                ++namePos;
            }
            if (namePos >= tokens.size()) continue;

            size_t nameEnd = namePos;
            while (nameEnd < tokens.size() && tokens[nameEnd] != "(" && tokens[nameEnd] != "{" && tokens[nameEnd] != ";") {
                if (!vietvm::compiler::isCallableNamePiece(tokens[nameEnd])) break;
                ++nameEnd;
            }
            if (nameEnd == namePos) continue;

            std::string fname = vietvm::compiler::joinNameTokens(tokens, namePos, nameEnd);
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

namespace vietvm::compiler {

CompilationArtifacts compilePipeline(
    const std::string &source,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    bool emitMainCall) {
    CompilationArtifacts artifacts;

    // Lexer normalization (including multi-word keywords) remains part of the
    // lexing stage, and preserves source spans when tokens are merged.
    artifacts.tokens = postProcessTokensWithSpans(tokenizeWithSpans(source));
    artifacts.ast = vietvm::frontend::parseTokens(artifacts.tokens);
    artifacts.semantic = analyzeSemantics(artifacts.ast);

    for (const SemanticDiagnostic &diagnostic : artifacts.semantic.diagnostics) {
        if (diagnostic.severity == SemanticDiagnosticSeverity::Error) {
            throw std::runtime_error(diagnostic.message);
        }
    }

    artifacts.ir = lowerToIr(artifacts.ast, artifacts.semantic);
    artifacts.optimization = optimizeIr(artifacts.ir);

    const DirectIrSupport directSupport = analyzeDirectIrSupport(artifacts.ir);
    artifacts.legacyFallbackRegions = directSupport.fallbackRegions;

    if (directSupport.supported) {
        artifacts.backend = BytecodeBackend::DirectIr;
        artifacts.bytecode = emitDirectBytecode(
            artifacts.ir, keywordMap, emitMainCall);
    } else {
        // A fallback is whole-program for now. Mixing backends before they
        // share explicit function/slot/fixup allocation would make IDs depend
        // on which regions happened to migrate.
        artifacts.backend = BytecodeBackend::LegacyTokenBridge;
        artifacts.bytecode = compileLegacyTokens(
            materializeIrTokens(artifacts.ir), keywordMap, emitMainCall);
    }
    return artifacts;
}

CompilationArtifacts compilePipeline(
    CompilationContext &context,
    const std::string &source,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    bool emitMainCall) {
    context.clear();
    resetCompilationState();

    try {
        CompilationArtifacts artifacts =
            compilePipeline(source, keywordMap, emitMainCall);

        context.stringPool = StringPool::getPool();
        context.functionBytecode = hamMap::hamBytecodeMap;
        context.functionNameIndices = hamMap::hamNameIndexMap;

        // The context now owns the complete runtime snapshot required by the
        // caller, so the legacy process-wide registries must not leak into the
        // next top-level compilation.
        resetCompilationState();
        return artifacts;
    } catch (...) {
        context.clear();
        resetCompilationState();
        throw;
    }
}

} // namespace vietvm::compiler

std::vector<Instruction> compileSource(const std::string& source,
                                       const std::unordered_map<std::string,Opcode>& keywordMap,
                                       bool emitMainCall)
{
    return vietvm::compiler::compilePipeline(source, keywordMap, emitMainCall).bytecode;
}
