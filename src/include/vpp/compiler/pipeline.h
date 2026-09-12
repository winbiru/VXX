#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "vpp/compiler/codegen.h"
#include "vpp/compiler/ir.h"
#include "vpp/compiler/optimizer.h"
#include "vpp/frontend/parser.h"
#include "vm/instruction.h"

namespace vietvm::compiler {

struct CompilationArtifacts {
    std::vector<vietvm::frontend::Token> tokens;
    vietvm::frontend::AstProgram ast;
    SemanticModel semantic;
    IrProgram ir;
    OptimizationReport optimization;
    std::size_t unsupportedDirectIrRegions = 0;
    std::vector<Instruction> bytecode;
};

// Per-top-level-compilation view of the process-wide registries. The compiler still
// uses process-wide registries internally while imports are being compiled,
// but callers no longer need to read those globals directly after a compile.
// This is the migration boundary for moving the registries fully into context.
struct CompilationContext {
    std::vector<std::string> stringPool;
    std::unordered_map<int, std::vector<Instruction>> functionBytecode;
    std::unordered_map<int, int> functionNameIndices;

    void clear();
};

// Runs the canonical compiler path:
// source -> lexer -> parser -> AST -> semantic analysis -> IR -> optimizer
// -> bytecode. It does not reset process-wide compiler registries; callers that begin
// a top-level compilation must use resetCompilationState() first.
CompilationArtifacts compilePipeline(
    const std::string &source,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    bool emitMainCall = true);

// Top-level entry point. It owns reset/error cleanup and snapshots the compiler
// registries into `context`. Recursive import compilation must keep using the
// context-less overload above so imported modules share the active session.
CompilationArtifacts compilePipeline(
    CompilationContext &context,
    const std::string &source,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    bool emitMainCall = true);

} // namespace vietvm::compiler
