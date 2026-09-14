// Compiler.cpp

#include "vm/instruction.h"
#include <filesystem>
#include <unordered_map>
#include <string>
#include "frontend/lexer.h"
#include "compiler/compileRegistry.h"
#include "common/storeString.h"
#include "vpp/compiler/pipeline.h"
#include "vpp/core/project_layout.h"

namespace vietvm::compiler {

namespace {

// Xóa transient import/access state của đúng registry được truyền vào, không dựa vào active context thread-local.
void clearTransientCompilationState(CompilationRegistryState &state) {
    clearImportedFiles(state);
    clearClassAccessState(state);
}

// Lấy các import source-file trực tiếp của AST hiện tại; compiler dùng danh sách này để biên dịch dependency trước module đang xét.
std::vector<vietvm::frontend::AstImportSpec> directLocalSourceImports(
    const vietvm::frontend::AstProgram &program) {
    std::vector<vietvm::frontend::AstImportSpec> imports;
    for (const vietvm::frontend::AstStatement &statement : program.statements) {
        if (statement.kind != vietvm::frontend::AstStatementKind::Import ||
            statement.importForm !=
                vietvm::frontend::AstImportForm::LocalSourceFile ||
            statement.importSpec.target.empty() ||
            !statement.importSpec.hasSemicolon) {
            continue;
        }
        if (vietvm::core::utf8Path(statement.importSpec.target).extension() != ".vi") {
            continue;
        }
        imports.push_back(statement.importSpec);
    }
    return imports;
}

} // namespace

// Đặt lại trạng thái toàn cục của compiler như StringPool, function map, import và class context để lần biên dịch mới độc lập.
void resetCompilationState() {
    activeCompilationRegistryState().clear();
}

// Xóa clear; hàm đưa cấu trúc trạng thái về rỗng để lần sử dụng tiếp theo không mang dữ liệu cũ.
void CompilationContext::clear() {
    CompilationRegistryState::clear();
}

} // namespace vietvm::compiler

namespace vietvm::compiler {

// Chạy pipeline trong registry đã có; recursive import gọi lại hàm này với `topLevel=false` để giữ chung session mà không dùng context ẩn.
CompilationArtifacts compilePipelineInRegistry(
    CompilationRegistryState &state,
    const std::string &source,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    bool emitMainCall,
    bool /*topLevel*/) {
    CompilationArtifacts artifacts;

    // Lexer normalization (including multi-word keywords) remains part of the
    // lexing stage, and preserves source spans when tokens are merged.
    artifacts.tokens = postProcessTokensWithSpans(tokenizeWithSpans(source));
    artifacts.ast = vietvm::frontend::parseTokens(artifacts.tokens);

    SemanticEnvironment semanticEnvironment;
    const auto localImports = directLocalSourceImports(artifacts.ast);
    if (!localImports.empty()) {
        namespace fs = std::filesystem;
        fs::path resolutionBase = state.importResolutionBase;
        if (resolutionBase.empty()) resolutionBase = fs::current_path();
        artifacts.moduleIndex = buildLocalModuleSemanticIndex(
            LocalModuleResolver(resolutionBase),
            std::string(kCurrentCompilationModuleIdentity),
            localImports,
            ModuleIndexMode::Recursive);
        semanticEnvironment = artifacts.moduleIndex->semanticEnvironmentFor(
            kCurrentCompilationModuleIdentity);
    }
    artifacts.semantic = analyzeSemantics(
        artifacts.ast, semanticEnvironment, ResolutionPolicy::PreserveLegacy);

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
        state, artifacts.ir, keywordMap, emitMainCall);
    return artifacts;
}

// Chạy pipeline tương thích trên legacy registry hiện hành; API này giữ cho test/compatibility caller cũ nhưng production CLI dùng overload có `CompilationContext`.
CompilationArtifacts compilePipeline(
    const std::string &source,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    bool emitMainCall) {
    return compilePipelineInRegistry(
        activeCompilationRegistryState(), source, keywordMap, emitMainCall, true);
}

// Chạy pipeline biên dịch từ source qua lexer, parser, semantic, IR, optimization và codegen; kết quả được gom vào `CompilationArtifacts`.
CompilationArtifacts compilePipeline(
    CompilationContext &context,
    const std::string &source,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    bool emitMainCall) {
    namespace fs = std::filesystem;

    if (context.importResolutionBase.empty()) {
        context.importResolutionBase = fs::current_path();
    } else {
        try {
            context.importResolutionBase =
                fs::absolute(context.importResolutionBase).lexically_normal();
        } catch (...) {
            context.importResolutionBase = context.importResolutionBase.lexically_normal();
        }
    }

    context.clear();

    try {
        CompilationArtifacts artifacts = compilePipelineInRegistry(
            context, source, keywordMap, emitMainCall, true);

        // Giữ StringPool/function/module initializer trong context cho CLI/runtime;
        // import/access stack chỉ cần trong lúc biên dịch nên xóa sau khi pipeline kết thúc.
        clearTransientCompilationState(context);
        return artifacts;
    } catch (...) {
        context.clear();
        throw;
    }
}

} // namespace vietvm::compiler

// Biên dịch chuỗi nguồn thành bytecode bằng pipeline hiện hành và trả về artifact phục vụ CLI/runtime.
std::vector<Instruction> compileSource(const std::string& source,
                                       const std::unordered_map<std::string,Opcode>& keywordMap,
                                       bool emitMainCall)
{
    vietvm::compiler::CompilationContext context;
    return vietvm::compiler::compilePipeline(
        context, source, keywordMap, emitMainCall).bytecode;
}
