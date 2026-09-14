#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/storeString.h"
#include "vpp/compiler/codegen.h"
#include "vpp/compiler/ir.h"
#include "vpp/compiler/module_graph.h"
#include "vpp/compiler/optimizer.h"
#include "vpp/frontend/parser.h"
#include "vm/instruction.h"

namespace vietvm::compiler {

// Gom kết quả các pha compiler trong một lần chạy: AST, semantic model, IR, bytecode và metadata backend để CLI/test có thể kiểm tra từng lớp.
struct CompilationArtifacts {
    std::vector<vietvm::frontend::Token> tokens;
    vietvm::frontend::AstProgram ast;
    std::optional<LocalModuleSemanticIndex> moduleIndex;
    SemanticModel semantic;
    IrProgram ir;
    OptimizationReport optimization;
    std::size_t unsupportedDirectIrRegions = 0;
    std::vector<Instruction> bytecode;
    std::vector<vietvm::runtime::RuntimeSourceLocation> bytecodeDebugInfo;
};

// Sở hữu trạng thái registry cùng thông tin phân giải import cho một pipeline; truyền context riêng giúp nhiều lần biên dịch không rò global state.
struct CompilationContext : CompilationRegistryState {
    // Xóa clear; hàm đưa cấu trúc trạng thái về rỗng để lần sử dụng tiếp theo không mang dữ liệu cũ.
    void clear();
};

// Chạy một pipeline trong registry/session đã có mà không reset state; dùng nội bộ cho
// recursive import để toàn bộ dependency dùng chung StringPool/function/module metadata.
CompilationArtifacts compilePipelineInRegistry(
    CompilationRegistryState &state,
    const std::string &source,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    bool emitMainCall,
    bool topLevel);

// API compatibility dùng registry thread-local cũ; production caller nên dùng overload nhận `CompilationContext`.
CompilationArtifacts compilePipeline(
    const std::string &source,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    bool emitMainCall = true);

// Chạy pipeline biên dịch từ source qua lexer, parser, semantic, IR, optimization và codegen; kết quả được gom vào `CompilationArtifacts`.
CompilationArtifacts compilePipeline(
    CompilationContext &context,
    const std::string &source,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    bool emitMainCall = true);

} // namespace vietvm::compiler
