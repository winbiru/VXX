// Compiler.cpp

#include "vm/instruction.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <unordered_set>
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
            left.runtimeName != right.runtimeName ||
            left.superclassName != right.superclassName ||
            left.implementedInterfaces != right.implementedInterfaces ||
            left.extendedInterfaces != right.extendedInterfaces ||
            left.methods != right.methods ||
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

// Legacy graphs retain their exact lowering/cache behavior. Explicit graphs
// retain each selected public function plus dependencies before constructing IR.
bool hasExplicitImportGraph(const CompilationRegistryState &state, bool topLevel) {
    if (topLevel || !hasSelectiveImports(state)) return false;
    const auto &edges = state.activeModuleSemanticIndex->graph.edges;
    return !edges.empty() && std::all_of(edges.begin(), edges.end(), [](const auto &edge) {
        return !edge.importSpec.selectedName.empty();
    });
}

vietvm::frontend::AstProgram retainSelectedModuleFunctions(
    const CompilationRegistryState &state,
    const vietvm::frontend::AstProgram &program,
    const SemanticModel &semantic) {
    using namespace vietvm::frontend;
    const auto &index = *state.activeModuleSemanticIndex;
    const auto *record = index.module(state.currentSemanticModuleIdentity);
    if (record == nullptr) return program;

    std::unordered_map<SymbolId, const AstStatement *> functions;
    std::unordered_map<std::size_t, SymbolId> declarations;
    for (const auto &statement : program.statements) {
        if (statement.kind != AstStatementKind::Function) continue;
        const int id = semantic.symbolForDeclaration(statement.tokenBegin);
        if (id < 0) return program;
        functions.emplace(static_cast<SymbolId>(id), &statement);
        declarations.emplace(statement.span.begin.offset, static_cast<SymbolId>(id));
    }
    std::unordered_set<SymbolId> retained;
    std::vector<SymbolId> pending;
    auto retain = [&](SymbolId id) {
        if (functions.count(id) != 0 && retained.insert(id).second) pending.push_back(id);
    };
    for (const auto &edge : index.graph.edges) {
        if (edge.importedIdentity != record->identity) continue;
        for (const auto &symbol : record->exports) {
            if (symbol.kind != SemanticSymbolKind::Function ||
                !matchesImportSelection(edge.importSpec, symbol.name) ||
                symbol.sourcePath != record->path) continue;
            const auto declaration = declarations.find(symbol.declaration.begin.offset);
            if (declaration != declarations.end()) retain(declaration->second);
        }
    }

    bool uncertain = false;
    std::unordered_set<ExprId> visited;
    std::function<void(const AstStatement &)> scanStatement;
    std::function<void(ExprId)> scanExpression = [&](ExprId id) {
        if (id == kInvalidExprId || !visited.insert(id).second) return;
        const auto *expression = program.expression(id);
        if (expression == nullptr) { uncertain = true; return; }
        if (const auto *binding = semantic.bindingForExpression(id)) retain(binding->symbol);
        if (expression->kind == AstExpressionKind::Call) {
            const auto *call = semantic.callBindingForExpression(id);
            if (call == nullptr || call->kind == CallTargetKind::DynamicName ||
                call->kind == CallTargetKind::Invalid) uncertain = true;
        }
        scanExpression(expression->operand);
        scanExpression(expression->left);
        scanExpression(expression->right);
        scanExpression(expression->callee);
        for (auto argument : expression->arguments) scanExpression(argument);
        for (auto element : expression->listElements) scanExpression(element);
        for (const auto &entry : expression->mapEntries) {
            scanExpression(entry.key);
            scanExpression(entry.value);
        }
        if (expression->kind == AstExpressionKind::Lambda) {
            const auto *lambda = program.lambda(expression->lambdaId);
            if (lambda == nullptr) { uncertain = true; return; }
            for (const auto &parameter : lambda->parameters)
                if (parameter.hasDefault) scanExpression(parameter.defaultValue);
            scanStatement(lambda->body);
        }
    };
    scanStatement = [&](const AstStatement &statement) {
        if (statement.kind == AstStatementKind::Unknown) uncertain = true;
        for (auto root : statement.expressionRoots) scanExpression(root);
        for (const auto &parameter : statement.parameters)
            if (parameter.hasDefault) scanExpression(parameter.defaultValue);
        for (const auto &arm : statement.switchArms) scanExpression(arm.label);
        for (const auto &child : statement.children) scanStatement(child);
    };
    // Initializers and every class/interface method remain roots, including
    // their defaults, closures and references to private top-level helpers.
    for (const auto &statement : program.statements)
        if (statement.kind != AstStatementKind::Function) scanStatement(statement);
    for (std::size_t cursor = 0; cursor < pending.size(); ++cursor)
        scanStatement(*functions.at(pending[cursor]));
    if (uncertain) return program;

    auto retainedProgram = program;
    auto &statements = retainedProgram.statements;
    statements.erase(std::remove_if(statements.begin(), statements.end(), [&](const auto &statement) {
        return statement.kind == AstStatementKind::Function &&
            retained.count(static_cast<SymbolId>(semantic.symbolForDeclaration(statement.tokenBegin))) == 0;
    }), statements.end());
    return retainedProgram;
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
    const bool selectiveRetention = hasExplicitImportGraph(state, topLevel);
    CachedImportedMidEnd *cachedMidEnd = nullptr;
    if (moduleSource != nullptr && !selectiveRetention) {
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

        // Analyze the full source first: unused functions still report all
        // semantic errors. Keep the lossless AST while lowering only retention.
        if (selectiveRetention) {
            const auto retained = retainSelectedModuleFunctions(state, artifacts.ast, artifacts.semantic);
            artifacts.ir = lowerToIr(retained, artifacts.semantic);
        } else {
            artifacts.ir = lowerToIr(artifacts.ast, artifacts.semantic);
        }
        artifacts.optimization = optimizeIr(artifacts.ir);

        const DirectIrSupport directSupport = analyzeDirectIrSupport(artifacts.ir);
        artifacts.unsupportedDirectIrRegions = directSupport.unsupportedRegions;

        if (moduleSource != nullptr && !selectiveRetention) {
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
