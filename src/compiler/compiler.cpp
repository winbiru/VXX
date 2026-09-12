// Compiler.cpp

#include <../include/vm/instruction.h>
#include <unordered_map>
#include <string>
#include <../include/frontend/lexer.h>
#include <compiler/compileRegistry.h>
#include "common/storeString.h"
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
    artifacts.unsupportedDirectIrRegions = directSupport.unsupportedRegions;
    artifacts.bytecode = emitDirectBytecode(
        artifacts.ir, keywordMap, emitMainCall);
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
        // caller, so the process-wide compiler registries must not leak into the
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
