#include "vpp/compiler/module_graph.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>
#include <utility>

#include "frontend/lexer.h"
#include "vpp/core/message_constants.h"
#include "vpp/core/project_layout.h"
#include "vpp/frontend/parser.h"

namespace vietvm::compiler {
namespace {

namespace fs = std::filesystem;

// Chuẩn hóa path thành dạng tuyệt đối theo lexical rules mà không yêu cầu file tồn tại; module graph dùng path này làm khóa ổn định.
fs::path absoluteLexical(const fs::path &path) {
    try {
        return fs::absolute(path).lexically_normal();
    } catch (...) {
        return path.lexically_normal();
    }
}

enum class VisitState {
    Active,
    Loaded,
};

// Phân tích mô-đun nguồn; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
vietvm::frontend::AstProgram parseModuleSource(const std::string &source) {
    return vietvm::frontend::parseTokens(
        postProcessTokensWithSpans(tokenizeWithSpans(source)));
}

// Trích các import local đã được parser nhận diện từ AST; hàm chỉ lấy `LocalSourceFile` để module graph không phải parse token thô.
std::vector<vietvm::frontend::AstImportSpec> structuredLocalImports(
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
        const fs::path target = vietvm::core::utf8Path(statement.importSpec.target);
        if (target.extension() != ".vi") {
            // Bare/package imports are resolved by the package resolver. The
            // local module graph deliberately indexes explicit source files.
            continue;
        }
        imports.push_back(statement.importSpec);
    }
    return imports;
}

// Kiểm tra điều kiện của `isModuleExportVisibility`.
bool isModuleExportVisibility(vietvm::frontend::AstVisibility visibility) noexcept {
    return visibility == vietvm::frontend::AstVisibility::Unspecified ||
           visibility == vietvm::frontend::AstVisibility::Public;
}

// Thu thập symbol được phép export của một module; hàm duyệt semantic model và giữ khai báo có visibility phù hợp cho module khác.
std::vector<ModuleExportSymbol> moduleExports(
    const vietvm::frontend::AstProgram &program) {
    std::vector<ModuleExportSymbol> exports;
    for (const vietvm::frontend::AstStatement &statement : program.statements) {
        if (statement.declarationName.empty() ||
            !isModuleExportVisibility(statement.visibility)) {
            continue;
        }

        SemanticSymbolKind kind;
        if (statement.kind == vietvm::frontend::AstStatementKind::Function) {
            kind = SemanticSymbolKind::Function;
        } else if (statement.kind == vietvm::frontend::AstStatementKind::Class) {
            kind = SemanticSymbolKind::Class;
        } else if (statement.kind == vietvm::frontend::AstStatementKind::Interface) {
            kind = SemanticSymbolKind::Interface;
        } else {
            continue;
        }
        exports.push_back(
            ModuleExportSymbol{statement.declarationName, kind, statement.span});
    }
    return exports;
}

// Thu thập các khai báo top-level có tên nhưng không được export để semantic có thể
// phân biệt truy cập nhầm symbol ẩn với một tên động hoàn toàn không tồn tại.
std::vector<ModuleExportSymbol> moduleHiddenSymbols(
    const vietvm::frontend::AstProgram &program) {
    std::vector<ModuleExportSymbol> hidden;
    for (const vietvm::frontend::AstStatement &statement : program.statements) {
        if (statement.declarationName.empty() ||
            isModuleExportVisibility(statement.visibility)) {
            continue;
        }

        SemanticSymbolKind kind;
        if (statement.kind == vietvm::frontend::AstStatementKind::Function) {
            kind = SemanticSymbolKind::Function;
        } else if (statement.kind == vietvm::frontend::AstStatementKind::Class) {
            kind = SemanticSymbolKind::Class;
        } else if (statement.kind == vietvm::frontend::AstStatementKind::Interface) {
            kind = SemanticSymbolKind::Interface;
        } else {
            continue;
        }
        hidden.push_back(
            ModuleExportSymbol{statement.declarationName, kind, statement.span});
    }
    return hidden;
}

// Ghép chuỗi identity của các module trong cycle để diagnostic chỉ ra chính xác
// đường import gây vòng lặp.
std::string formatImportCycle(const std::vector<std::string> &stack,
                              const std::string &repeatedIdentity) {
    const auto begin = std::find(stack.begin(), stack.end(), repeatedIdentity);
    std::ostringstream chain;
    bool first = true;
    for (auto it = begin; it != stack.end(); ++it) {
        if (!first) chain << " -> ";
        chain << *it;
        first = false;
    }
    if (!first) chain << " -> ";
    chain << repeatedIdentity;
    return chain.str();
}

// Giữ trạng thái DFS khi dựng module graph, gồm resolver, graph đang tạo và dấu vết module để phát hiện import cycle.
struct BuildContext {
    const LocalModuleResolver &resolver;
    const LocalModuleImportScanner &scanner;
    LocalModuleGraph graph;
    std::unordered_map<std::string, VisitState> states;
    std::vector<std::string> activeStack;

    // Duyệt một module khi xây dependency graph; hàm đánh dấu trạng thái DFS, đọc import con và phát hiện chu trình trước khi thêm cạnh.
    void visit(const std::string &importerIdentity,
               const vietvm::frontend::AstImportSpec &importSpec) {
        const LocalModuleLocation location = resolver.resolve(importSpec);

        LocalModuleEdge edge;
        edge.importerIdentity = importerIdentity;
        edge.importedIdentity = location.identity;
        edge.importSpec = importSpec;

        const auto existing = states.find(location.identity);
        if (existing != states.end()) {
            if (existing->second == VisitState::Active) {
                throw std::runtime_error(vietvm::messages::formatMessage(
                    vietvm::messages::kImportModuleCycle,
                    {formatImportCycle(activeStack, location.identity)}));
            }
            edge.action = LocalModuleEdgeAction::DuplicateNoOp;
            graph.edges.push_back(std::move(edge));
            return;
        }

        // Mark before reading or parsing.  A recursive import can now observe
        // this identity as Active, while any failure below rolls the mark back.
        states.emplace(location.identity, VisitState::Active);
        activeStack.push_back(location.identity);
        edge.action = LocalModuleEdgeAction::Load;
        graph.edges.push_back(std::move(edge));

        try {
            LocalModuleSource module = resolver.read(location);
            const std::vector<vietvm::frontend::AstImportSpec> imports =
                scanner(module.source, module.path);
            graph.modules.push_back(std::move(module));

            for (const vietvm::frontend::AstImportSpec &nestedImport : imports) {
                visit(location.identity, nestedImport);
            }
            states[location.identity] = VisitState::Loaded;
            activeStack.pop_back();
        } catch (...) {
            if (!activeStack.empty() && activeStack.back() == location.identity) {
                activeStack.pop_back();
            }
            states.erase(location.identity);
            throw;
        }
    }
};

} // namespace

// Khởi tạo `LocalModuleResolver` từ các tham số đầu vào; constructor lưu trạng thái ban đầu cần thiết để các phương thức của đối tượng hoạt động nhất quán.
LocalModuleResolver::LocalModuleResolver(fs::path resolutionBase)
    : resolutionBase_(absoluteLexical(std::move(resolutionBase))) {}

// Trả thư mục gốc dùng để phân giải import tương đối; resolver chuẩn hóa giá trị constructor thành path tuyệt đối/lexical ổn định.
const fs::path &LocalModuleResolver::resolutionBase() const noexcept {
    return resolutionBase_;
}

// Phân giải phân giải; hàm lần theo metadata/phạm vi liên quan để biến tham chiếu đầu vào thành đích cụ thể.
LocalModuleLocation LocalModuleResolver::resolve(
    const vietvm::frontend::AstImportSpec &importSpec) const {
    const fs::path requested = vietvm::core::utf8Path(importSpec.target);
    // `resolutionBase_` defaults to the process cwd for legacy compatibility,
    // but an explicit base is authoritative (and makes graph construction
    // deterministic for embedders and tests whose process cwd is elsewhere).
    fs::path resolved = absoluteLexical(resolutionBase_ / requested);

    if (!fs::exists(resolved)) {
        for (fs::path directory = resolutionBase_;;
             directory = directory.parent_path()) {
            const fs::path candidate = directory / requested;
            if (fs::exists(candidate)) {
                resolved = absoluteLexical(candidate);
                break;
            }
            if (directory == directory.parent_path()) break;
        }
    }

    resolved = resolved.lexically_normal();
    return {resolved, resolved.u8string()};
}

// Đọc read; hàm lấy nội dung từ nguồn tương ứng, kiểm tra lỗi cần thiết rồi trả dữ liệu đã đọc.
LocalModuleSource LocalModuleResolver::read(
    const LocalModuleLocation &location) const {
    std::ifstream input(location.path);
    if (!input.is_open()) {
        throw std::runtime_error(vietvm::messages::formatMessage(
            vietvm::messages::kImportCannotOpenFile, {location.identity}));
    }

    std::ostringstream source;
    source << input.rdbuf();
    if (input.bad()) {
        throw std::runtime_error(vietvm::messages::formatMessage(
            vietvm::messages::kImportCannotOpenFile, {location.identity}));
    }
    return {location.path, location.identity, source.str()};
}

// Trả tên văn bản ổn định cho cục bộ mô-đun edge action; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
const char *localModuleEdgeActionName(LocalModuleEdgeAction action) noexcept {
    switch (action) {
        case LocalModuleEdgeAction::Load: return "load";
        case LocalModuleEdgeAction::DuplicateNoOp: return "duplicate_no_op";
        case LocalModuleEdgeAction::CycleNoOp: return "cycle_no_op";
    }
    return "unknown";
}

// Khởi tạo `LocalModuleGraphBuilder` từ các tham số đầu vào; constructor lưu trạng thái ban đầu cần thiết để các phương thức của đối tượng hoạt động nhất quán.
LocalModuleGraphBuilder::LocalModuleGraphBuilder(
    LocalModuleResolver resolver,
    LocalModuleImportScanner importScanner)
    : resolver_(std::move(resolver)),
      importScanner_(std::move(importScanner)) {
    if (!importScanner_) {
        throw std::invalid_argument(
            "LocalModuleGraphBuilder requires an import scanner");
    }
}

// Dựng build; hàm tổng hợp các phần tử đầu vào thành cấu trúc hoàn chỉnh, đồng thời thiết lập các quan hệ/chỉ mục cần thiết.
LocalModuleGraph LocalModuleGraphBuilder::build(
    std::string entryIdentity,
    const std::vector<vietvm::frontend::AstImportSpec> &rootImports) const {
    BuildContext context{resolver_, importScanner_, {}, {}, {}};
    context.graph.entryIdentity = std::move(entryIdentity);
    for (const vietvm::frontend::AstImportSpec &rootImport : rootImports) {
        context.visit(context.graph.entryIdentity, rootImport);
    }
    return std::move(context.graph);
}

// Tra `RuntimeModule` theo tên hoặc chỉ mục; hàm trả con trỏ/tham chiếu tới record đang được `ModuleTable` quản lý.
const LocalModuleSemanticRecord *LocalModuleSemanticIndex::module(
    std::string_view identity) const noexcept {
    for (const LocalModuleSemanticRecord &candidate : modules) {
        if (candidate.identity == identity) return &candidate;
    }
    return nullptr;
}

// Tra symbol được export từ một module semantic record; hàm tìm theo tên public và trả metadata cần cho module khác import.
const ModuleExportSymbol *LocalModuleSemanticIndex::exportedSymbol(
    std::string_view moduleIdentity,
    std::string_view name) const noexcept {
    const LocalModuleSemanticRecord *record = module(moduleIdentity);
    if (record == nullptr) return nullptr;
    for (const ModuleExportSymbol &symbol : record->exports) {
        if (symbol.name == name) return &symbol;
    }
    return nullptr;
}

// Tạo `SemanticEnvironment` cho một module; hàm gom các export từ dependency đã phân giải thành external symbol mà analyzer có thể nhìn thấy.
SemanticEnvironment LocalModuleSemanticIndex::semanticEnvironmentFor(
    std::string_view importerIdentity) const {
    SemanticEnvironment environment;
    std::unordered_set<std::string> seen;

    for (const LocalModuleEdge &edge : graph.edges) {
        if (edge.importerIdentity != importerIdentity) continue;
        const LocalModuleSemanticRecord *record = module(edge.importedIdentity);
        if (record == nullptr) continue;

        for (const ModuleExportSymbol &exported : record->exports) {
            const std::string importedName = edge.importSpec.alias.empty()
                ? exported.name
                : edge.importSpec.alias + "." + exported.name;
            if (!seen.insert(importedName).second) continue;
            environment.importedSymbols.push_back(
                SemanticExternalSymbol{importedName,
                                       exported.kind,
                                       exported.declaration});
        }
        for (const ModuleExportSymbol &hidden : record->hiddenSymbols) {
            const std::string importedName = edge.importSpec.alias.empty()
                ? hidden.name
                : edge.importSpec.alias + "." + hidden.name;
            environment.hiddenImportedSymbols.push_back(
                SemanticHiddenImportedSymbol{importedName,
                                             record->identity,
                                             hidden.declaration});
        }
    }
    return environment;
}

// Bổ sung các export được chuyển tiếp bởi `công khai nhập`. Vì graph đã cấm cycle,
// thứ tự reverse DFS đảm bảo dependency có bề mặt export hoàn chỉnh trước importer.
void applyModuleReExports(LocalModuleSemanticIndex &index) {
    for (auto recordIt = index.modules.rbegin(); recordIt != index.modules.rend(); ++recordIt) {
        LocalModuleSemanticRecord &record = *recordIt;
        std::unordered_set<std::string> seen;
        for (const ModuleExportSymbol &symbol : record.exports) seen.insert(symbol.name);

        for (const LocalModuleEdge &edge : index.graph.edges) {
            if (edge.importerIdentity != record.identity || !edge.importSpec.reExport) continue;
            const LocalModuleSemanticRecord *dependency = index.module(edge.importedIdentity);
            if (dependency == nullptr) continue;
            for (const ModuleExportSymbol &symbol : dependency->exports) {
                ModuleExportSymbol forwarded = symbol;
                if (!edge.importSpec.alias.empty()) {
                    forwarded.name = edge.importSpec.alias + "." + forwarded.name;
                }
                if (seen.insert(forwarded.name).second) {
                    record.exports.push_back(std::move(forwarded));
                }
            }
        }
    }
}

// Dựng cục bộ mô-đun ngữ nghĩa chỉ số; hàm tổng hợp các phần tử đầu vào thành cấu trúc hoàn chỉnh, đồng thời thiết lập các quan hệ/chỉ mục cần thiết.
LocalModuleSemanticIndex buildLocalModuleSemanticIndex(
    LocalModuleResolver resolver,
    std::string entryIdentity,
    const std::vector<vietvm::frontend::AstImportSpec> &rootImports,
    ModuleIndexMode mode) {
    if (mode == ModuleIndexMode::DirectOnly) {
        LocalModuleSemanticIndex index;
        index.entryIdentity = entryIdentity;
        index.graph.entryIdentity = entryIdentity;
        std::unordered_set<std::string> loaded;

        for (const auto &candidate : rootImports) {
            const LocalModuleLocation location = resolver.resolve(candidate);
            if (!fs::exists(location.path)) continue;

            LocalModuleEdge edge;
            edge.importerIdentity = entryIdentity;
            edge.importedIdentity = location.identity;
            edge.importSpec = candidate;
            if (!loaded.insert(location.identity).second) {
                edge.action = LocalModuleEdgeAction::DuplicateNoOp;
                index.graph.edges.push_back(std::move(edge));
                continue;
            }

            edge.action = LocalModuleEdgeAction::Load;
            index.graph.edges.push_back(std::move(edge));
            LocalModuleSource source = resolver.read(location);
            const vietvm::frontend::AstProgram program = parseModuleSource(source.source);
            index.modules.push_back(
                LocalModuleSemanticRecord{source.path,
                                          source.identity,
                                          moduleExports(program),
                                          moduleHiddenSymbols(program)});
            index.graph.modules.push_back(std::move(source));
        }
        return index;
    }

    const LocalModuleResolver scanResolver = resolver;
    LocalModuleGraphBuilder graphBuilder(
        std::move(resolver),
        [scanResolver](const std::string &source, const fs::path &) {
            std::vector<vietvm::frontend::AstImportSpec> resolved;
            for (const auto &candidate :
                 structuredLocalImports(parseModuleSource(source))) {
                if (fs::exists(scanResolver.resolve(candidate).path)) {
                    resolved.push_back(candidate);
                }
            }
            return resolved;
        });

    std::vector<vietvm::frontend::AstImportSpec> resolvedRoots;
    resolvedRoots.reserve(rootImports.size());
    for (const auto &candidate : rootImports) {
        if (fs::exists(scanResolver.resolve(candidate).path)) {
            resolvedRoots.push_back(candidate);
        }
    }

    LocalModuleSemanticIndex index;
    index.entryIdentity = entryIdentity;
    index.graph = graphBuilder.build(std::move(entryIdentity), resolvedRoots);
    index.modules.reserve(index.graph.modules.size());
    for (const LocalModuleSource &source : index.graph.modules) {
        const vietvm::frontend::AstProgram program = parseModuleSource(source.source);
        index.modules.push_back(
            LocalModuleSemanticRecord{source.path,
                                      source.identity,
                                      moduleExports(program),
                                      moduleHiddenSymbols(program)});
    }
    applyModuleReExports(index);
    return index;
}

// Trả tên văn bản ổn định cho mô-đun khởi tạo trạng thái; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
const char *moduleInitializationStateName(
    ModuleInitializationState state) noexcept {
    switch (state) {
        case ModuleInitializationState::Uninitialized: return "uninitialized";
        case ModuleInitializationState::Initializing: return "initializing";
        case ModuleInitializationState::Initialized: return "initialized";
        case ModuleInitializationState::Failed: return "failed";
    }
    return "unknown";
}

// Khởi tạo `ModuleInitializationTracker` từ các tham số đầu vào; constructor lưu trạng thái ban đầu cần thiết để các phương thức của đối tượng hoạt động nhất quán.
ModuleInitializationTracker::ModuleInitializationTracker(
    const LocalModuleSemanticIndex &index) {
    for (const LocalModuleSemanticRecord &module : index.modules) {
        states_.emplace(module.identity,
                        ModuleInitializationState::Uninitialized);
    }
}

// Trả trạng thái hiện tại của đối tượng quản lý; hàm chỉ tra dữ liệu nội bộ tương ứng với khóa/module được yêu cầu.
std::optional<ModuleInitializationState> ModuleInitializationTracker::state(
    std::string_view identity) const noexcept {
    const auto found = states_.find(std::string(identity));
    if (found == states_.end()) return std::nullopt;
    return found->second;
}

// Chuyển thực thể sang trạng thái đang khởi tạo/đang xử lý; hàm kiểm tra trạng thái trước đó để ngăn bắt đầu lặp sai quy trình.
bool ModuleInitializationTracker::begin(std::string_view identity) {
    const auto found = states_.find(std::string(identity));
    if (found == states_.end() ||
        found->second != ModuleInitializationState::Uninitialized) {
        return false;
    }
    found->second = ModuleInitializationState::Initializing;
    return true;
}

// Đánh dấu thao tác/module đã hoàn tất thành công; hàm cập nhật trạng thái và thứ tự hoàn tất dùng cho các lần tra cứu sau.
bool ModuleInitializationTracker::complete(std::string_view identity) {
    const auto found = states_.find(std::string(identity));
    if (found == states_.end() ||
        found->second != ModuleInitializationState::Initializing) {
        return false;
    }
    found->second = ModuleInitializationState::Initialized;
    return true;
}

// Đánh dấu thao tác/module thất bại; hàm lưu trạng thái lỗi để caller không xem thực thể là đã khởi tạo thành công.
bool ModuleInitializationTracker::fail(std::string_view identity) {
    const auto found = states_.find(std::string(identity));
    if (found == states_.end() ||
        found->second != ModuleInitializationState::Initializing) {
        return false;
    }
    found->second = ModuleInitializationState::Failed;
    return true;
}

} // namespace vietvm::compiler
