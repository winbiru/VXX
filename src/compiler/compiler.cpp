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

// Gắn tạm một `CompilationRegistryState` làm registry đang hoạt động theo RAII; constructor lưu registry cũ và destructor khôi phục lại để các pipeline lồng nhau không giẫm trạng thái của nhau.
class CompilationRegistryBinding {
public:
    // Kích hoạt registry mới và giữ con trỏ registry trước đó để có thể phục hồi chính xác khi rời scope.
    explicit CompilationRegistryBinding(CompilationRegistryState &state)
        : previous_(setActiveCompilationRegistryState(&state)) {}

    // Khôi phục registry đã hoạt động trước khi binding được tạo, kể cả khi pipeline thoát bằng exception.
    ~CompilationRegistryBinding() {
        setActiveCompilationRegistryState(previous_);
    }

    // Cấm sao chép để không có hai RAII guard cùng nghĩ rằng chúng sở hữu quyền phục hồi một registry trước đó.
    CompilationRegistryBinding(const CompilationRegistryBinding &) = delete;
    // Cấm phép gán vì việc thay ownership giữa chừng sẽ phá thứ tự khôi phục registry theo stack scope.
    CompilationRegistryBinding &operator=(const CompilationRegistryBinding &) = delete;

private:
    CompilationRegistryState *previous_;
};

thread_local std::size_t pipelineDepth = 0;

// Theo dõi độ sâu pipeline theo RAII để biết lượt biên dịch hiện tại có phải top-level hay đang chạy lồng do import; constructor tăng bộ đếm và destructor luôn giảm lại.
class PipelineDepthGuard {
public:
    // Đánh dấu `topLevel_` khi đây là pipeline ngoài cùng, sau đó tăng `pipelineDepth` cho các lượt compile lồng bên trong.
    PipelineDepthGuard() : topLevel_(pipelineDepth++ == 0) {}
    // Giảm lại độ sâu pipeline khi rời scope để lần biên dịch tiếp theo quan sát đúng trạng thái nesting.
    ~PipelineDepthGuard() { --pipelineDepth; }

    // Cho biết guard đại diện cho pipeline ngoài cùng; caller dùng cờ này để chỉ reset/finalize trạng thái toàn cục ở đúng tầng.
    bool topLevel() const noexcept { return topLevel_; }

private:
    bool topLevel_ = false;
};

// Xóa transient compilation trạng thái; hàm đưa cấu trúc trạng thái về rỗng để lần sử dụng tiếp theo không mang dữ liệu cũ.
void clearTransientCompilationState() {
    clearImportedFiles();
    clearClassAccessState();
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
    StringPool::clear();
    clearTransientCompilationState();
    hamMap::bytecodeMap().clear();
    hamMap::clearHamNameIndexMap();
    hamMap::resetHamIdCounter();
}

// Xóa clear; hàm đưa cấu trúc trạng thái về rỗng để lần sử dụng tiếp theo không mang dữ liệu cũ.
void CompilationContext::clear() {
    CompilationRegistryState::clear();
}

} // namespace vietvm::compiler

namespace vietvm::compiler {

// Chạy pipeline biên dịch từ source qua lexer, parser, semantic, IR, optimization và codegen; kết quả được gom vào `CompilationArtifacts`.
CompilationArtifacts compilePipeline(
    const std::string &source,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    bool emitMainCall) {
    PipelineDepthGuard depthGuard;
    CompilationArtifacts artifacts;

    // Lexer normalization (including multi-word keywords) remains part of the
    // lexing stage, and preserves source spans when tokens are merged.
    artifacts.tokens = postProcessTokensWithSpans(tokenizeWithSpans(source));
    artifacts.ast = vietvm::frontend::parseTokens(artifacts.tokens);

    SemanticEnvironment semanticEnvironment;
    const auto localImports = directLocalSourceImports(artifacts.ast);
    if (depthGuard.topLevel() && !localImports.empty()) {
        namespace fs = std::filesystem;
        fs::path resolutionBase = activeCompilationRegistryState().importResolutionBase;
        if (resolutionBase.empty()) resolutionBase = fs::current_path();
        constexpr std::string_view kEntryIdentity = "entry://current-compilation";
        artifacts.moduleIndex = buildLocalModuleSemanticIndex(
            LocalModuleResolver(resolutionBase),
            std::string(kEntryIdentity),
            localImports,
            ModuleIndexMode::DirectOnly);
        semanticEnvironment = artifacts.moduleIndex->semanticEnvironmentFor(
            kEntryIdentity);
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
        artifacts.ir, keywordMap, emitMainCall);
    return artifacts;
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
    CompilationRegistryBinding registryBinding(context);
    resetCompilationState();

    try {
        CompilationArtifacts artifacts =
            compilePipeline(source, keywordMap, emitMainCall);

        // StringPool/function state is already owned by `context`; only transient
        // import/access-control state remains thread-local during compilation.
        clearTransientCompilationState();
        return artifacts;
    } catch (...) {
        context.clear();
        clearTransientCompilationState();
        throw;
    }
}

} // namespace vietvm::compiler

// Biên dịch chuỗi nguồn thành bytecode bằng pipeline hiện hành và trả về artifact phục vụ CLI/runtime.
std::vector<Instruction> compileSource(const std::string& source,
                                       const std::unordered_map<std::string,Opcode>& keywordMap,
                                       bool emitMainCall)
{
    return vietvm::compiler::compilePipeline(source, keywordMap, emitMainCall).bytecode;
}
