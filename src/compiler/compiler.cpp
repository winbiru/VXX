// Compiler.cpp

#include "vm/instruction.h"
#include <cstdlib>
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

struct CachedImportedMidEnd {
    std::string source;
    SemanticEnvironment environment;
    SemanticModel semantic;
    IrProgram ir;
    OptimizationReport optimization;
    std::size_t unsupportedDirectIrRegions = 0;
};

thread_local std::unordered_map<std::string, CachedImportedMidEnd>
    importedMidEndCache;
constexpr std::size_t kImportedMidEndCacheLimit = 512;

bool samePosition(const vietvm::frontend::SourcePosition &lhs,
                  const vietvm::frontend::SourcePosition &rhs) noexcept {
    return lhs.offset == rhs.offset &&
           lhs.line == rhs.line &&
           lhs.column == rhs.column;
}

bool sameSpan(const vietvm::frontend::SourceSpan &lhs,
              const vietvm::frontend::SourceSpan &rhs) noexcept {
    return samePosition(lhs.begin, rhs.begin) &&
           samePosition(lhs.end, rhs.end);
}

bool sameSemanticEnvironment(const SemanticEnvironment &lhs,
                             const SemanticEnvironment &rhs) noexcept {
    if (lhs.importedSymbols.size() != rhs.importedSymbols.size() ||
        lhs.hiddenImportedSymbols.size() != rhs.hiddenImportedSymbols.size() ||
        lhs.nativeCallables != rhs.nativeCallables) {
        return false;
    }

    for (std::size_t i = 0; i < lhs.importedSymbols.size(); ++i) {
        const SemanticExternalSymbol &left = lhs.importedSymbols[i];
        const SemanticExternalSymbol &right = rhs.importedSymbols[i];
        if (left.name != right.name ||
            left.kind != right.kind ||
            left.sourceIdentity != right.sourceIdentity ||
            !sameSpan(left.declaration, right.declaration)) {
            return false;
        }
    }

    for (std::size_t i = 0; i < lhs.hiddenImportedSymbols.size(); ++i) {
        const SemanticHiddenImportedSymbol &left =
            lhs.hiddenImportedSymbols[i];
        const SemanticHiddenImportedSymbol &right =
            rhs.hiddenImportedSymbols[i];
        if (left.name != right.name ||
            left.moduleIdentity != right.moduleIdentity ||
            !sameSpan(left.declaration, right.declaration)) {
            return false;
        }
    }
    return true;
}

const std::string *currentImportedModuleSource(
    const CompilationRegistryState &state,
    bool topLevel) noexcept {
    if (topLevel ||
        state.activeModuleSemanticIndex == nullptr ||
        state.currentSemanticModuleIdentity.empty()) {
        return nullptr;
    }
    return state.activeModuleSemanticIndex->moduleSource(
        state.currentSemanticModuleIdentity);
}

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

static CompilationArtifacts finishPipelineInRegistry(
    CompilationRegistryState &state,
    CompilationArtifacts artifacts,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    bool emitEntryPointCall,
    bool topLevel) {
    SemanticEnvironment semanticEnvironment;
    const auto localImports = directLocalSourceImports(artifacts.ast);
    if (!localImports.empty()) {
        if (!topLevel && state.activeModuleSemanticIndex != nullptr &&
            !state.currentSemanticModuleIdentity.empty()) {
            semanticEnvironment = state.activeModuleSemanticIndex->semanticEnvironmentFor(
                state.currentSemanticModuleIdentity);
        } else {
            namespace fs = std::filesystem;
            fs::path resolutionBase = state.importResolutionBase;
            if (resolutionBase.empty()) resolutionBase = fs::current_path();
            std::optional<fs::path> installationHome;
            if (const char *vppHome = std::getenv(vietvm::core::kEnvVppHome)) {
                installationHome = vietvm::core::utf8Path(vppHome);
            }
            artifacts.moduleIndex = buildLocalModuleSemanticIndex(
                LocalModuleResolver(resolutionBase, installationHome),
                std::string(kCurrentCompilationModuleIdentity),
                localImports,
                ModuleIndexMode::Recursive);
            semanticEnvironment = artifacts.moduleIndex->semanticEnvironmentFor(
                kCurrentCompilationModuleIdentity);
        }
    }
    const std::string *moduleSource =
        currentImportedModuleSource(state, topLevel);
    CachedImportedMidEnd *cachedMidEnd = nullptr;
    if (moduleSource != nullptr) {
        const auto found =
            importedMidEndCache.find(state.currentSemanticModuleIdentity);
        if (found != importedMidEndCache.end() &&
            found->second.source == *moduleSource &&
            sameSemanticEnvironment(found->second.environment,
                                    semanticEnvironment)) {
            cachedMidEnd = &found->second;
        }
    }

    if (cachedMidEnd != nullptr) {
        artifacts.semantic = cachedMidEnd->semantic;
        artifacts.ir = cachedMidEnd->ir;
        artifacts.optimization = cachedMidEnd->optimization;
        artifacts.unsupportedDirectIrRegions =
            cachedMidEnd->unsupportedDirectIrRegions;
    } else {
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

        if (moduleSource != nullptr) {
            if (importedMidEndCache.size() >= kImportedMidEndCacheLimit &&
                importedMidEndCache.find(state.currentSemanticModuleIdentity) ==
                    importedMidEndCache.end()) {
                importedMidEndCache.clear();
            }
            importedMidEndCache[state.currentSemanticModuleIdentity] =
                CachedImportedMidEnd{
                    *moduleSource,
                    semanticEnvironment,
                    artifacts.semantic,
                    artifacts.ir,
                    artifacts.optimization,
                    artifacts.unsupportedDirectIrRegions};
        }
    }
    const LocalModuleSemanticIndex *previousModuleIndex =
        state.activeModuleSemanticIndex;
    if (topLevel && artifacts.moduleIndex.has_value()) {
        state.activeModuleSemanticIndex = &*artifacts.moduleIndex;
    }
    try {
        artifacts.bytecode = emitDirectBytecode(
            state, artifacts.ir, keywordMap, emitEntryPointCall);
    } catch (...) {
        state.activeModuleSemanticIndex = previousModuleIndex;
        throw;
    }
    state.activeModuleSemanticIndex = previousModuleIndex;
    artifacts.bytecodeDebugInfo = state.rootBytecodeDebugInfo;
    return artifacts;
}

// Chạy pipeline trong registry đã có; recursive import gọi lại hàm này với
// `topLevel=false` để giữ chung session mà không dùng context ẩn.
CompilationArtifacts compilePipelineInRegistry(
    CompilationRegistryState &state,
    const std::string &source,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    bool emitEntryPointCall,
    bool topLevel) {
    CompilationArtifacts artifacts;
    artifacts.tokens = postProcessTokensWithSpans(tokenizeWithSpans(source));
    artifacts.ast = vietvm::frontend::parseTokens(artifacts.tokens);
    return finishPipelineInRegistry(
        state, std::move(artifacts), keywordMap, emitEntryPointCall, topLevel);
}

CompilationArtifacts compileParsedPipelineInRegistry(
    CompilationRegistryState &state,
    const vietvm::frontend::AstProgram &program,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    bool emitEntryPointCall,
    bool topLevel) {
    CompilationArtifacts artifacts;
    artifacts.ast = program;
    return finishPipelineInRegistry(
        state, std::move(artifacts), keywordMap, emitEntryPointCall, topLevel);
}

// Chạy pipeline tương thích trên legacy registry hiện hành; API này giữ cho test/compatibility caller cũ nhưng production CLI dùng overload có `CompilationContext`.
CompilationArtifacts compilePipeline(
    const std::string &source,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    bool emitEntryPointCall) {
    return compilePipelineInRegistry(
        activeCompilationRegistryState(), source, keywordMap, emitEntryPointCall, true);
}

// Chạy pipeline biên dịch từ source qua lexer, parser, semantic, IR, optimization và codegen; kết quả được gom vào `CompilationArtifacts`.
CompilationArtifacts compilePipeline(
    CompilationContext &context,
    const std::string &source,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    bool emitEntryPointCall) {
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
            context, source, keywordMap, emitEntryPointCall, true);

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
                                       bool emitEntryPointCall)
{
    vietvm::compiler::CompilationContext context;
    return vietvm::compiler::compilePipeline(
        context, source, keywordMap, emitEntryPointCall).bytecode;
}
