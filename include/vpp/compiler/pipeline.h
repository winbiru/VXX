#pragma once

#include <string>
#include <unordered_map>
#include <vector>

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
    std::vector<Instruction> bytecode;
};

// Runs the canonical compiler path:
// source -> lexer -> parser -> AST -> semantic analysis -> IR -> optimizer
// -> bytecode.  It does not reset global legacy registries; callers that begin
// a top-level compilation must use resetCompilationState() first.
CompilationArtifacts compilePipeline(
    const std::string &source,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    bool emitMainCall = true);

} // namespace vietvm::compiler
